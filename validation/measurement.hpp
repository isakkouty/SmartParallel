#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

namespace smart::validation
{
struct MeasurementConfig
{
    std::size_t warmup_rounds = 2;
    std::size_t measured_rounds = 9;
    double confidence_z = 1.96;
    std::uint64_t random_seed = 0x5a17c9e3ULL;
};

struct MeasurementStatistics
{
    std::size_t samples = 0;
    double median_ms = 0.0;
    double mean_ms = 0.0;
    double trimmed_mean_ms = 0.0;
    double standard_deviation_ms = 0.0;
    double median_absolute_deviation_ms = 0.0;

    double p90_ms = 0.0;
    double confidence_low_ms = 0.0;
    double confidence_high_ms = 0.0;
};

inline double percentile(std::vector<double> values, double probability)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const double position =
        std::clamp(probability, 0.0, 1.0) * static_cast<double>(values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
    const double fraction = position - static_cast<double>(lower);
    return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

inline MeasurementStatistics summarize(const std::vector<double>& raw_samples,
                                       double confidence_z = 1.96)
{
    MeasurementStatistics result;
    if (raw_samples.empty())
        return result;

    std::vector<double> samples = raw_samples;
    std::sort(samples.begin(), samples.end());
    result.samples = samples.size();
    result.median_ms = percentile(samples, 0.50);
    result.p90_ms = percentile(samples, 0.90);
    result.mean_ms =
        std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());

    std::size_t trim = samples.size() >= 7 ? samples.size() / 10 : 0;
    if (trim * 2 >= samples.size())
        trim = 0;
    const auto trimmed_begin = samples.begin() + static_cast<std::ptrdiff_t>(trim);
    const auto trimmed_end = samples.end() - static_cast<std::ptrdiff_t>(trim);
    result.trimmed_mean_ms = std::accumulate(trimmed_begin, trimmed_end, 0.0)
                             / static_cast<double>(std::distance(trimmed_begin, trimmed_end));

    double squared = 0.0;
    for (double sample : samples)
    {
        const double delta = sample - result.mean_ms;
        squared += delta * delta;
    }
    result.standard_deviation_ms =
        samples.size() > 1 ? std::sqrt(squared / static_cast<double>(samples.size() - 1)) : 0.0;

    std::vector<double> deviations;
    deviations.reserve(samples.size());
    for (double sample : samples)
        deviations.push_back(std::abs(sample - result.median_ms));
    result.median_absolute_deviation_ms = percentile(deviations, 0.50);

    // Normal approximation for the sampling distribution of a median.
    // The robust MAD estimate is preferred; standard deviation is a
    // fallback for nearly identical samples.
    const double robust_sigma = result.median_absolute_deviation_ms > 0.0
                                    ? 1.4826 * result.median_absolute_deviation_ms
                                    : result.standard_deviation_ms;
    const double median_standard_error =
        samples.size() > 1 ? 1.2533 * robust_sigma / std::sqrt(static_cast<double>(samples.size()))
                           : 0.0;
    const double half_width = std::max(0.0, confidence_z * median_standard_error);
    result.confidence_low_ms = std::max(0.0, result.median_ms - half_width);
    result.confidence_high_ms = result.median_ms + half_width;
    return result;
}

inline bool confidence_intervals_overlap(const MeasurementStatistics& left,
                                         const MeasurementStatistics& right)
{
    return left.confidence_low_ms <= right.confidence_high_ms
           && right.confidence_low_ms <= left.confidence_high_ms;
}

template <typename Runner>
std::vector<MeasurementStatistics>
measure_interleaved(std::size_t candidate_count, const MeasurementConfig& config, Runner&& runner)
{
    std::vector<std::vector<double>> samples(candidate_count);
    std::vector<std::size_t> order(candidate_count);
    std::iota(order.begin(), order.end(), 0);
    std::mt19937_64 random(config.random_seed);

    for (std::size_t warmup = 0; warmup < config.warmup_rounds; ++warmup)
    {
        std::shuffle(order.begin(), order.end(), random);
        for (std::size_t index : order)
            static_cast<void>(runner(index, true));
    }

    for (std::size_t round = 0; round < std::max<std::size_t>(1, config.measured_rounds); ++round)
    {
        std::shuffle(order.begin(), order.end(), random);
        for (std::size_t index : order)
            samples[index].push_back(runner(index, false));
    }

    std::vector<MeasurementStatistics> results;
    results.reserve(candidate_count);
    for (const auto& candidate_samples : samples)
        results.push_back(summarize(candidate_samples, config.confidence_z));
    return results;
}

struct RegretMetrics
{
    double median_percent = 0.0;
    double p90_percent = 0.0;
    double worst_percent = 0.0;
    std::size_t catastrophic_10_percent = 0;
    std::size_t catastrophic_25_percent = 0;
};

inline RegretMetrics regret_metrics(const std::vector<double>& regrets)
{
    RegretMetrics result;
    if (regrets.empty())
        return result;
    result.median_percent = percentile(regrets, 0.50);
    result.p90_percent = percentile(regrets, 0.90);
    result.worst_percent = *std::max_element(regrets.begin(), regrets.end());
    for (double regret : regrets)
    {
        result.catastrophic_10_percent += regret > 10.0 ? 1u : 0u;
        result.catastrophic_25_percent += regret > 25.0 ? 1u : 0u;
    }
    return result;
}
} // namespace smart::validation
