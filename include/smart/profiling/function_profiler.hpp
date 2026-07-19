#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <smart/workload/observation.hpp>
#include <vector>

namespace smart
{
enum class FunctionProfileSamplingMode
{
    None,
    Direct,
    IsolatedCopies
};

enum class FunctionProfileUnavailableReason
{
    None,
    EmptyRange,
    ValueNotCopyConstructible,
    CallableNotCopyConstructible,
    CallableNotInvocableOnCopy,
    IterationCountOverflow
};

enum class ProfileStopReason
{
    None,
    ConfidenceReached,
    TimeBudgetReached,
    InvocationBudgetReached,
    MaximumSamplesReached,
    WorkloadExhausted,
    MeasurementUnreliable
};

struct FunctionProfile
{
    bool available = false;

    FunctionProfileSamplingMode sampling_mode = FunctionProfileSamplingMode::None;

    FunctionProfileUnavailableReason unavailable_reason = FunctionProfileUnavailableReason::None;

    ObservationMetadata metadata{ObservationSource::Unavailable,
                                 ObservationConfidence::Unavailable};

    ProfileStopReason stop_reason = ProfileStopReason::None;

    std::size_t samples = 0;

    std::size_t measured_batches = 0;
    std::size_t callback_invocations = 0;
    std::size_t chosen_batch_size = 0;

    double profiling_elapsed_ms = 0.0;
    double timer_overhead_ms = 0.0;
    double measurement_floor_ms = 0.0;

    double signal_to_floor_ratio = 0.0;
    bool measurement_reliable = false;

    // Warm-up and spatial observations are intentionally diagnostic in
    // Phase 1. They do not choose an execution strategy.
    std::size_t first_batch_size = 0;
    double first_batch_ms_per_iteration = 0.0;
    double steady_state_ms_per_iteration = 0.0;
    double warmup_ratio = 0.0;
    double estimated_setup_cost_ms = 0.0;
    bool warmup_detected = false;

    std::size_t local_samples = 0;
    std::size_t distributed_samples = 0;
    double local_median_ms_per_iteration = 0.0;
    double distributed_median_ms_per_iteration = 0.0;
    double distributed_to_local_ratio = 0.0;
    double early_region_median_ms_per_iteration = 0.0;

    double middle_region_median_ms_per_iteration = 0.0;
    double late_region_median_ms_per_iteration = 0.0;
    double regional_cost_ratio = 0.0;
    bool spatial_observations_available = false;

    double avg_ms_per_iteration = 0.0;
    double median_ms_per_iteration = 0.0;

    double trimmed_mean_ms_per_iteration = 0.0;
    double stddev_ms_per_iteration = 0.0;
    double coefficient_of_variation = 0.0;
    double p95_ms_per_iteration = 0.0;
    double tail_ratio = 0.0;
    double max_ms_per_iteration = 0.0;

    double estimated_total_work_ms = 0.0;
    double estimated_parallel_overhead_ms = 0.0;

    double parallel_worthiness = 0.0;

    // Compatibility field retained for the current decision rules.
    double instability_ratio = 0.0;
    bool stable = false;
};

class FunctionProfiler
{
  public:
    struct Config
    {
        std::size_t min_samples = 8;
        std::size_t max_samples = 64;
        std::size_t batch_size = 8;
        std::size_t max_batch_size = 4096;
        std::size_t max_callback_invocations = 4096;
        std::size_t local_sample_count = 3;

        double measured_parallel_overhead_ms = 1.0;
        double stable_ratio = 2.5;
        double stable_cv = 0.25;
        double stable_tail_ratio = 1.75;
        double target_batch_duration_ms = 0.05;
        double max_profile_time_ms = 2.0;

        double relative_error_target = 0.10;
        double trim_fraction = 0.10;
        double minimum_signal_to_floor_ratio = 5.0;
        double warmup_ratio_threshold = 1.50;
    };

    template <typename Function>
    FunctionProfile profile_index_range(std::size_t begin,
                                        std::size_t end,
                                        Function&& func,
                                        Config config = {}) const
    {
        FunctionProfile profile;

        if (end <= begin)
        {
            profile.unavailable_reason = FunctionProfileUnavailableReason::EmptyRange;
            return profile;
        }

        normalize_config(config);

        const std::size_t total = end - begin;
        const ProfilerCalibration calibration = profiler_calibration();

        profile.timer_overhead_ms = calibration.timer_overhead_ms;
        profile.measurement_floor_ms = calibration.measurement_floor_ms;

        const auto profile_start = std::chrono::steady_clock::now();

        std::size_t batch_size = std::min(config.batch_size, total);
        batch_size = std::max<std::size_t>(1, batch_size);

        double first_pilot_cost = 0.0;
        std::size_t first_pilot_batch = 0;
        double final_pilot_raw_ms = 0.0;
        double final_pilot_net_ms = 0.0;
        std::size_t final_pilot_batch = batch_size;

        for (;;)
        {
            const std::size_t pilot_batch = std::min(batch_size, total);

            const BatchMeasurement pilot =
                measure_batch(begin, pilot_batch, func, calibration.timer_overhead_ms);

            profile.callback_invocations += pilot_batch;

            if (first_pilot_batch == 0)
            {
                first_pilot_batch = pilot_batch;
                first_pilot_cost = pilot.net_ms / static_cast<double>(pilot_batch);
            }

            final_pilot_raw_ms = pilot.raw_ms;
            final_pilot_net_ms = pilot.net_ms;
            final_pilot_batch = pilot_batch;

            if (pilot.raw_ms >= config.target_batch_duration_ms
                || batch_size >= config.max_batch_size || pilot_batch >= total
                || profile.callback_invocations >= config.max_callback_invocations)
            {
                break;
            }

            const std::size_t doubled =
                batch_size > config.max_batch_size / 2 ? config.max_batch_size : batch_size * 2;

            if (doubled == batch_size)
            {
                break;
            }

            batch_size = doubled;
        }

        profile.first_batch_size = first_pilot_batch;
        profile.first_batch_ms_per_iteration = first_pilot_cost;
        profile.chosen_batch_size = final_pilot_batch;
        profile.signal_to_floor_ratio = calibration.measurement_floor_ms > 0.0
                                            ? final_pilot_raw_ms / calibration.measurement_floor_ms
                                            : std::numeric_limits<double>::infinity();

        profile.measurement_reliable =
            final_pilot_net_ms > 0.0
            && profile.signal_to_floor_ratio >= config.minimum_signal_to_floor_ratio;

        std::vector<SampleRecord> records;
        records.reserve(config.max_samples);

        if (final_pilot_batch > 0)
        {
            records.push_back({final_pilot_net_ms / static_cast<double>(final_pilot_batch),
                               begin,
                               SampleKind::Pilot,
                               region_for_index(0, total)});
        }

        std::size_t local_index = 0;
        std::size_t distributed_sample = 0;

        while (records.size() < config.max_samples)
        {
            if (profile.callback_invocations + profile.chosen_batch_size
                > config.max_callback_invocations)
            {
                profile.stop_reason = ProfileStopReason::InvocationBudgetReached;
                break;
            }

            const double elapsed_ms = elapsed_since_ms(profile_start);
            if (elapsed_ms >= config.max_profile_time_ms)
            {
                profile.stop_reason = ProfileStopReason::TimeBudgetReached;
                break;
            }

            SampleKind kind = SampleKind::Distributed;
            std::size_t relative_index = 0;

            if (local_index < config.local_sample_count)
            {
                kind = SampleKind::Local;
                relative_index = local_sample_index(
                    local_index, config.local_sample_count, total, profile.chosen_batch_size);
                ++local_index;
            }
            else
            {
                const std::size_t distributed_target =
                    config.max_samples > 1 + config.local_sample_count
                        ? config.max_samples - 1 - config.local_sample_count
                        : 1;

                relative_index = jittered_stratified_index(
                    distributed_sample, distributed_target, total, profile.chosen_batch_size);
                ++distributed_sample;
            }

            const std::size_t start_index = begin + relative_index;
            const std::size_t batch_count = std::min(profile.chosen_batch_size, end - start_index);

            if (batch_count == 0)
            {
                profile.stop_reason = ProfileStopReason::WorkloadExhausted;
                break;
            }

            const BatchMeasurement measurement =
                measure_batch(start_index, batch_count, func, calibration.timer_overhead_ms);

            profile.callback_invocations += batch_count;
            records.push_back({measurement.net_ms / static_cast<double>(batch_count),
                               start_index,
                               kind,
                               region_for_index(relative_index, total)});

            if (records.size() >= config.min_samples
                && relative_standard_error(values_from(records)) <= config.relative_error_target)
            {
                profile.stop_reason = ProfileStopReason::ConfidenceReached;
                break;
            }
        }

        if (profile.stop_reason == ProfileStopReason::None)
        {
            profile.stop_reason = records.size() >= config.max_samples
                                      ? ProfileStopReason::MaximumSamplesReached
                                      : ProfileStopReason::WorkloadExhausted;
        }

        profile.profiling_elapsed_ms = elapsed_since_ms(profile_start);
        finalize_profile(profile, records, total, config);
        return profile;
    }

  private:
    struct ProfilerCalibration
    {
        double timer_overhead_ms = 0.0;
        double measurement_floor_ms = 0.0;
    };

    struct BatchMeasurement
    {
        double raw_ms = 0.0;
        double net_ms = 0.0;
    };

    enum class SampleKind
    {
        Pilot,
        Local,
        Distributed
    };

    enum class SampleRegion
    {
        Early = 0,
        Middle = 1,
        Late = 2
    };

    struct SampleRecord
    {
        double cost_ms_per_iteration = 0.0;
        std::size_t start_index = 0;
        SampleKind kind = SampleKind::Distributed;
        SampleRegion region = SampleRegion::Middle;
    };

    static void normalize_config(Config& config)
    {
        config.min_samples = std::max<std::size_t>(1, config.min_samples);
        config.max_samples = std::max(config.min_samples, config.max_samples);
        config.batch_size = std::max<std::size_t>(1, config.batch_size);
        config.max_batch_size = std::max(config.batch_size, config.max_batch_size);
        config.max_callback_invocations =
            std::max(config.batch_size, config.max_callback_invocations);
        config.local_sample_count = std::min(config.local_sample_count,
                                             config.max_samples > 1 ? config.max_samples - 1 : 0);
        config.max_profile_time_ms = std::max(0.01, config.max_profile_time_ms);
        config.target_batch_duration_ms = std::max(0.001, config.target_batch_duration_ms);
        config.relative_error_target = std::max(0.001, config.relative_error_target);
        config.trim_fraction = std::clamp(config.trim_fraction, 0.0, 0.45);
        config.warmup_ratio_threshold = std::max(1.0, config.warmup_ratio_threshold);
    }

    static ProfilerCalibration profiler_calibration()
    {
        static const ProfilerCalibration calibration = []
        {
            ProfilerCalibration result;
            constexpr std::size_t trials = 256;
            double minimum_positive = std::numeric_limits<double>::infinity();
            double sum = 0.0;

            for (std::size_t i = 0; i < trials; ++i)
            {
                const auto start = std::chrono::steady_clock::now();
                const auto stop = std::chrono::steady_clock::now();
                const double elapsed =
                    std::chrono::duration<double, std::milli>(stop - start).count();

                sum += elapsed;
                if (elapsed > 0.0 && elapsed < minimum_positive)
                {
                    minimum_positive = elapsed;
                }
            }

            result.timer_overhead_ms = sum / trials;
            result.measurement_floor_ms = std::isfinite(minimum_positive)
                                              ? std::max(minimum_positive, result.timer_overhead_ms)
                                              : std::max(1e-6, result.timer_overhead_ms);
            return result;
        }();

        return calibration;
    }

    template <typename Function>
    static BatchMeasurement measure_batch(std::size_t start_index,
                                          std::size_t batch_count,
                                          Function& func,
                                          double timer_overhead_ms)
    {
        const auto start = std::chrono::steady_clock::now();

        for (std::size_t offset = 0; offset < batch_count; ++offset)
        {
            func(start_index + offset);
        }

        const auto stop = std::chrono::steady_clock::now();
        const double raw_ms = std::chrono::duration<double, std::milli>(stop - start).count();

        BatchMeasurement measurement;
        measurement.raw_ms = raw_ms;
        measurement.net_ms = std::max(0.0, raw_ms - timer_overhead_ms);
        return measurement;
    }

    static double elapsed_since_ms(const std::chrono::steady_clock::time_point& start)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
            .count();
    }

    static double mean(const std::vector<double>& values)
    {
        if (values.empty())
        {
            return 0.0;
        }

        return std::accumulate(values.begin(), values.end(), 0.0)
               / static_cast<double>(values.size());
    }

    static double standard_deviation(const std::vector<double>& values, double average)
    {
        if (values.size() < 2)
        {
            return 0.0;
        }

        double squared_sum = 0.0;
        for (double value : values)
        {
            const double delta = value - average;
            squared_sum += delta * delta;
        }

        return std::sqrt(squared_sum / static_cast<double>(values.size() - 1));
    }

    static double relative_standard_error(const std::vector<double>& values)
    {
        const double average = mean(values);
        if (average <= 0.0 || values.size() < 2)
        {
            return std::numeric_limits<double>::infinity();
        }

        const double deviation = standard_deviation(values, average);
        return deviation / (average * std::sqrt(static_cast<double>(values.size())));
    }

    static double percentile(const std::vector<double>& sorted, double fraction)
    {
        if (sorted.empty())
        {
            return 0.0;
        }

        const double position = fraction * static_cast<double>(sorted.size() - 1);
        const std::size_t lower = static_cast<std::size_t>(std::floor(position));
        const std::size_t upper = static_cast<std::size_t>(std::ceil(position));

        if (lower == upper)
        {
            return sorted[lower];
        }

        const double weight = position - static_cast<double>(lower);
        return sorted[lower] * (1.0 - weight) + sorted[upper] * weight;
    }

    static double trimmed_mean(const std::vector<double>& sorted, double trim_fraction)
    {
        if (sorted.empty())
        {
            return 0.0;
        }

        const std::size_t trim_count =
            static_cast<std::size_t>(static_cast<double>(sorted.size()) * trim_fraction);

        if (trim_count * 2 >= sorted.size())
        {
            return mean(sorted);
        }

        const auto first = sorted.begin() + static_cast<std::ptrdiff_t>(trim_count);
        const auto last = sorted.end() - static_cast<std::ptrdiff_t>(trim_count);

        return std::accumulate(first, last, 0.0) / static_cast<double>(std::distance(first, last));
    }

    static ObservationConfidence determine_confidence(const FunctionProfile& profile,
                                                      const Config& config,
                                                      double relative_error)
    {
        if (!profile.measurement_reliable)
        {
            return ObservationConfidence::Low;
        }

        if (profile.samples >= config.min_samples && relative_error <= config.relative_error_target
            && profile.stop_reason == ProfileStopReason::ConfidenceReached)
        {
            return ObservationConfidence::High;
        }

        if (profile.samples >= config.min_samples)
        {
            return ObservationConfidence::Medium;
        }

        return ObservationConfidence::Low;
    }

    static void finalize_profile(FunctionProfile& profile,
                                 const std::vector<SampleRecord>& records,
                                 std::size_t total,
                                 const Config& config)
    {
        std::vector<double> samples = values_from(records);

        if (samples.empty())
        {
            profile.stop_reason = ProfileStopReason::MeasurementUnreliable;
            profile.metadata = {ObservationSource::Unavailable, ObservationConfidence::Unavailable};
            return;
        }

        std::sort(samples.begin(), samples.end());

        profile.available = true;
        profile.sampling_mode = FunctionProfileSamplingMode::Direct;
        profile.unavailable_reason = FunctionProfileUnavailableReason::None;
        profile.samples = samples.size();
        profile.measured_batches = samples.size();

        profile.avg_ms_per_iteration = mean(samples);
        profile.median_ms_per_iteration = percentile(samples, 0.50);
        profile.trimmed_mean_ms_per_iteration = trimmed_mean(samples, config.trim_fraction);
        profile.stddev_ms_per_iteration = standard_deviation(samples, profile.avg_ms_per_iteration);

        if (profile.avg_ms_per_iteration > 0.0)
        {
            profile.coefficient_of_variation =
                profile.stddev_ms_per_iteration / profile.avg_ms_per_iteration;
        }

        profile.p95_ms_per_iteration = percentile(samples, 0.95);
        profile.max_ms_per_iteration = samples.back();

        if (profile.median_ms_per_iteration > 0.0)
        {
            profile.tail_ratio = profile.p95_ms_per_iteration / profile.median_ms_per_iteration;
        }

        finalize_warmup_observations(profile, records, config);
        finalize_spatial_observations(profile, records);

        const double primary_cost = profile.trimmed_mean_ms_per_iteration > 0.0
                                        ? profile.trimmed_mean_ms_per_iteration
                                        : profile.avg_ms_per_iteration;

        profile.estimated_total_work_ms = primary_cost * static_cast<double>(total);
        profile.estimated_parallel_overhead_ms = config.measured_parallel_overhead_ms;

        if (profile.estimated_parallel_overhead_ms > 0.0)
        {
            profile.parallel_worthiness =
                profile.estimated_total_work_ms / profile.estimated_parallel_overhead_ms;
        }

        if (profile.avg_ms_per_iteration > 0.0)
        {
            profile.instability_ratio = profile.max_ms_per_iteration / profile.avg_ms_per_iteration;
        }

        profile.stable = profile.coefficient_of_variation <= config.stable_cv
                         && profile.tail_ratio <= config.stable_tail_ratio
                         && profile.instability_ratio <= config.stable_ratio;

        const double relative_error = relative_standard_error(samples);
        profile.metadata.source = ObservationSource::Sampled;
        profile.metadata.confidence = determine_confidence(profile, config, relative_error);
    }

    static void finalize_warmup_observations(FunctionProfile& profile,
                                             const std::vector<SampleRecord>& records,
                                             const Config& config)
    {
        std::vector<double> steady_samples;
        steady_samples.reserve(records.size());

        for (const SampleRecord& record : records)
        {
            if (record.kind != SampleKind::Pilot)
            {
                steady_samples.push_back(record.cost_ms_per_iteration);
            }
        }

        if (steady_samples.empty())
        {
            profile.steady_state_ms_per_iteration = profile.median_ms_per_iteration;
            return;
        }

        std::sort(steady_samples.begin(), steady_samples.end());
        profile.steady_state_ms_per_iteration = percentile(steady_samples, 0.50);

        if (profile.steady_state_ms_per_iteration > 0.0)
        {
            profile.warmup_ratio =
                profile.first_batch_ms_per_iteration / profile.steady_state_ms_per_iteration;
        }

        profile.warmup_detected = profile.measurement_reliable && steady_samples.size() >= 3
                                  && profile.warmup_ratio >= config.warmup_ratio_threshold;

        if (profile.first_batch_ms_per_iteration > profile.steady_state_ms_per_iteration)
        {
            profile.estimated_setup_cost_ms =
                (profile.first_batch_ms_per_iteration - profile.steady_state_ms_per_iteration)
                * static_cast<double>(profile.first_batch_size);
        }
    }

    static void finalize_spatial_observations(FunctionProfile& profile,
                                              const std::vector<SampleRecord>& records)
    {
        std::vector<double> local;
        std::vector<double> distributed;
        std::array<std::vector<double>, 3> regions;

        for (const SampleRecord& record : records)
        {
            if (record.kind == SampleKind::Pilot)
            {
                continue;
            }

            if (record.kind == SampleKind::Local)
            {
                local.push_back(record.cost_ms_per_iteration);
            }
            else
            {
                distributed.push_back(record.cost_ms_per_iteration);
            }

            regions[static_cast<std::size_t>(record.region)].push_back(
                record.cost_ms_per_iteration);
        }

        profile.local_samples = local.size();
        profile.distributed_samples = distributed.size();

        if (!local.empty())
        {
            std::sort(local.begin(), local.end());
            profile.local_median_ms_per_iteration = percentile(local, 0.50);
        }

        if (!distributed.empty())
        {
            std::sort(distributed.begin(), distributed.end());
            profile.distributed_median_ms_per_iteration = percentile(distributed, 0.50);
        }

        if (profile.local_median_ms_per_iteration > 0.0
            && profile.distributed_median_ms_per_iteration > 0.0)
        {
            profile.distributed_to_local_ratio =
                profile.distributed_median_ms_per_iteration / profile.local_median_ms_per_iteration;
        }

        std::array<double, 3> medians{0.0, 0.0, 0.0};
        double minimum_positive = std::numeric_limits<double>::infinity();
        double maximum_value = 0.0;
        std::size_t available_regions = 0;

        for (std::size_t i = 0; i < regions.size(); ++i)
        {
            if (regions[i].empty())
            {
                continue;
            }

            std::sort(regions[i].begin(), regions[i].end());
            medians[i] = percentile(regions[i], 0.50);
            if (medians[i] > 0.0)
            {
                minimum_positive = std::min(minimum_positive, medians[i]);
                maximum_value = std::max(maximum_value, medians[i]);
                ++available_regions;
            }
        }

        profile.early_region_median_ms_per_iteration = medians[0];
        profile.middle_region_median_ms_per_iteration = medians[1];
        profile.late_region_median_ms_per_iteration = medians[2];

        if (available_regions >= 2 && std::isfinite(minimum_positive) && minimum_positive > 0.0)
        {
            profile.regional_cost_ratio = maximum_value / minimum_positive;
        }

        profile.spatial_observations_available =
            !local.empty() || !distributed.empty() || available_regions >= 2;
    }

    static std::vector<double> values_from(const std::vector<SampleRecord>& records)
    {
        std::vector<double> values;
        values.reserve(records.size());
        for (const SampleRecord& record : records)
        {
            values.push_back(record.cost_ms_per_iteration);
        }
        return values;
    }

    static SampleRegion region_for_index(std::size_t index, std::size_t total)
    {
        if (total <= 1)
        {
            return SampleRegion::Middle;
        }

        const double fraction = static_cast<double>(index) / static_cast<double>(total - 1);

        if (fraction < 1.0 / 3.0)
        {
            return SampleRegion::Early;
        }
        if (fraction < 2.0 / 3.0)
        {
            return SampleRegion::Middle;
        }
        return SampleRegion::Late;
    }

    static std::size_t local_sample_index(std::size_t sample,
                                          std::size_t sample_count,
                                          std::size_t total,
                                          std::size_t batch_count)
    {
        if (total <= batch_count)
        {
            return 0;
        }

        const std::size_t maximum_start = total - batch_count;
        const std::size_t anchor = maximum_start / 2;
        const std::size_t group_width = sample_count > 0 ? sample_count * batch_count : batch_count;
        const std::size_t group_begin = anchor > group_width / 2 ? anchor - group_width / 2 : 0;

        const std::size_t offset = sample * batch_count;
        return std::min(maximum_start, group_begin + offset);
    }

    static std::uint64_t deterministic_hash(std::uint64_t value)
    {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    static std::size_t jittered_stratified_index(std::size_t sample,
                                                 std::size_t sample_count,
                                                 std::size_t total,
                                                 std::size_t batch_count)
    {
        if (total <= batch_count || sample_count == 0)
        {
            return 0;
        }

        const std::size_t maximum_start = total - batch_count;
        const std::size_t stratum_begin = sample * (maximum_start + 1) / sample_count;
        const std::size_t stratum_end = ((sample + 1) * (maximum_start + 1) / sample_count) - 1;

        if (stratum_end <= stratum_begin)
        {
            return std::min(maximum_start, stratum_begin);
        }

        const std::size_t span = stratum_end - stratum_begin + 1;
        const std::size_t jitter =
            static_cast<std::size_t>(deterministic_hash(sample + total) % span);

        return std::min(maximum_start, stratum_begin + jitter);
    }
};
} // namespace smart
