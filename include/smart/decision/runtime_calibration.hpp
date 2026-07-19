#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <smart/core/config.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/execution/executor.hpp>
#include <smart/hardware/hardware.hpp>
#include <smart/workload/workload.hpp>
#include <utility>
#include <vector>

namespace smart
{
enum class RuntimeWorkloadClass
{
    General,
    ComputeLike,
    StreamingLike
};

struct RuntimeCalibrationQuery
{
    std::size_t iterations = 1;

    double per_iteration_ms = 0.0;
    double streaming_strength = 0.0;
    double irregular_strength = 0.0;
    double branch_strength = 0.0;
    double large_record_strength = 0.0;
};

struct BackendRuntimeCalibrationPoint
{
    ExecutionEngineType engine = ExecutionEngineType::ThreadPool;
    std::size_t workers = 1;

    // Scheduler-only model: base + log_slope * log2(total_chunks + 1).
    // The legacy names are retained for source compatibility.
    double base_overhead_ms = 0.03;
    double log_slope_ms = 0.002;
    double overhead_relative_uncertainty = 0.25;

    // Useful-work speedups. Scheduler overhead is removed before these
    // values are calculated, so it is not counted again by the predictor.
    double cheap_small_speedup = 1.0;
    double cheap_large_speedup = 1.0;
    double compute_speedup = 1.0;
    double streaming_speedup = 1.0;
    double cheap_small_confidence = 0.0;
    double cheap_large_confidence = 0.0;

    double compute_confidence = 0.0;
    double streaming_confidence = 0.0;
    double speedup_relative_uncertainty = 0.20;
};

struct BackendRuntimeCalibration
{
    std::vector<BackendRuntimeCalibrationPoint> points;
    bool measured = false;
};

namespace detail
{
inline volatile std::uint64_t runtime_calibration_sink = 0;

inline double calibration_now_ms()
{
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();
}

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

struct TimingObservation
{
    double median_ms = 0.0;
    double relative_mad = 1.0;
};

inline ExecutionPlan calibration_plan(ExecutionEngineType engine,
                                      ExecutionStrategy strategy,
                                      std::size_t workers,
                                      std::size_t chunk_size = 0)
{
    ExecutionPlan plan;
    plan.engine = engine;
    plan.strategy = strategy;
    plan.parallel = strategy != ExecutionStrategy::Sequential;
    plan.job_count = plan.parallel ? std::max<std::size_t>(1, workers) : 1;
    plan.chunk_size =
        strategy == ExecutionStrategy::DynamicChunks ? std::max<std::size_t>(1, chunk_size) : 0;
    return plan;
}

template <typename Function>
inline TimingObservation
measure_plan_total(const ExecutionPlan& plan, std::size_t iterations, int runs, Function function)
{
    Workload workload;
    workload.iterations = iterations;

    // Two warmups reduce first-use thread creation and code/cache noise.
    execute_workload(workload, plan, function);
    execute_workload(workload, plan, function);

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(runs));
    for (int run = 0; run < runs; ++run)
    {
        const double start = calibration_now_ms();
        execute_workload(workload, plan, function);
        samples.push_back(calibration_now_ms() - start);
    }

    TimingObservation result;
    result.median_ms = percentile(samples, 0.50);
    std::vector<double> deviations;
    deviations.reserve(samples.size());
    for (double sample : samples)
        deviations.push_back(std::abs(sample - result.median_ms));
    const double mad = percentile(deviations, 0.50);
    result.relative_mad =
        result.median_ms > 0.0 ? std::clamp(1.4826 * mad / result.median_ms, 0.0, 2.0) : 1.0;
    return result;
}

inline std::vector<std::size_t> calibration_worker_counts(std::size_t maximum_workers)
{
    std::vector<std::size_t> counts;
    if (maximum_workers <= 1)
        return counts;

    for (std::size_t workers = 2; workers < maximum_workers;)
    {
        counts.push_back(workers);
        if (workers > maximum_workers / 2)
            break;
        workers *= 2;
    }
    if (counts.empty() || counts.back() != maximum_workers)
        counts.push_back(maximum_workers);
    counts.erase(std::unique(counts.begin(), counts.end()), counts.end());
    return counts;
}

inline std::size_t chunk_for_count(std::size_t iterations, std::size_t chunks)
{
    return std::max<std::size_t>(
        1, (iterations + std::max<std::size_t>(1, chunks) - 1) / std::max<std::size_t>(1, chunks));
}

inline double overhead_from_curve(double base, double slope, std::size_t total_chunks)
{
    return std::max(
        0.0,
        base
            + slope * std::log2(static_cast<double>(std::max<std::size_t>(1, total_chunks)) + 1.0));
}

inline double useful_speedup(double sequential_ms,
                             double parallel_total_ms,
                             double scheduler_overhead_ms,
                             std::size_t workers)
{
    if (!std::isfinite(sequential_ms) || !std::isfinite(parallel_total_ms) || sequential_ms <= 0.0
        || parallel_total_ms <= 0.0)
    {
        return 1.0;
    }

    // Throughput probes include launch/task overhead in wall time.
    // Remove the separately calibrated scheduler component before
    // turning the probe into useful-work speedup. The old model kept
    // it in the speedup and then added it again to every candidate.
    // Never let a noisy scheduler estimate erase most of a short
    // throughput probe. At most 25% of the observed parallel time may
    // be removed, and useful-work speedup is capped at worker count.
    // This prevents sub-millisecond probes from producing 20x speedup
    // on 16 workers after subtracting a nearly equal overhead term.
    const double removable_overhead =
        std::min(std::max(0.0, scheduler_overhead_ms), parallel_total_ms * 0.25);
    const double ideal_floor = sequential_ms / std::max(1.0, static_cast<double>(workers));
    const double parallel_useful_ms = std::max(ideal_floor, parallel_total_ms - removable_overhead);
    return std::clamp(
        sequential_ms / parallel_useful_ms, 1.0, std::max(1.0, static_cast<double>(workers)));
}

inline double smoothstep(double low, double high, double value);

struct SpeedupObservation
{
    double speedup = 1.0;
    double relative_uncertainty = 1.0;
    double confidence = 0.0;
};

template <typename Function>
inline SpeedupObservation measure_useful_speedup(ExecutionEngineType engine,
                                                 ExecutionStrategy strategy,
                                                 std::size_t workers,
                                                 std::size_t iterations,
                                                 int runs,
                                                 double overhead_base,
                                                 double overhead_slope,
                                                 Function function)
{
    const ExecutionPlan sequential =
        calibration_plan(ExecutionEngineType::ThreadPool, ExecutionStrategy::Sequential, 1);
    const std::size_t desired_chunks = std::max<std::size_t>(workers * 8, workers);
    const std::size_t chunk = chunk_for_count(iterations, desired_chunks);
    const ExecutionPlan parallel = calibration_plan(engine, strategy, workers, chunk);

    const TimingObservation sequential_timing =
        measure_plan_total(sequential, iterations, runs, function);

    const TimingObservation parallel_timing =
        measure_plan_total(parallel, iterations, runs, function);
    const std::size_t actual_chunks =
        strategy == ExecutionStrategy::DynamicChunks ? (iterations + chunk - 1) / chunk : workers;

    const double overhead = overhead_from_curve(overhead_base, overhead_slope, actual_chunks);
    const double speedup =
        useful_speedup(sequential_timing.median_ms, parallel_timing.median_ms, overhead, workers);

    const double uncertainty = std::clamp(
        std::hypot(sequential_timing.relative_mad, parallel_timing.relative_mad), 0.0, 2.0);

    const double signal_ms = std::min(sequential_timing.median_ms, parallel_timing.median_ms);

    const double duration_confidence = smoothstep(0.25, 5.0, signal_ms);
    const double overhead_fraction =
        parallel_timing.median_ms > 0.0 ? std::clamp(overhead / parallel_timing.median_ms, 0.0, 1.0)
                                        : 1.0;
    const double overhead_confidence = 1.0 - smoothstep(0.15, 0.55, overhead_fraction);

    const double stability_confidence = std::clamp(1.0 / (1.0 + 4.0 * uncertainty), 0.0, 1.0);

    SpeedupObservation observation;
    observation.speedup = speedup;
    observation.relative_uncertainty = uncertainty;
    observation.confidence =
        std::clamp(duration_confidence * overhead_confidence * stability_confidence, 0.0, 1.0);
    return observation;
}

inline BackendRuntimeCalibrationPoint
measure_point(ExecutionEngineType engine, ExecutionStrategy strategy, std::size_t workers)
{
    constexpr std::size_t overhead_iterations = 65'536;

    constexpr std::size_t cheap_small_iterations = 65'536;
    constexpr std::size_t cheap_large_iterations = 1'048'576;
    constexpr std::size_t compute_iterations = 32'768;
    constexpr std::size_t stream_iterations = 1'048'576;

    constexpr int overhead_runs = 9;
    constexpr int throughput_runs = 5;

    const ExecutionPlan sequential =
        calibration_plan(ExecutionEngineType::ThreadPool, ExecutionStrategy::Sequential, 1);

    // A compiler barrier leaves the callback observable without adding
    // a memory stream to what should be scheduler calibration.
    const auto scheduler_function = [](std::size_t)
    {
        std::atomic_signal_fence(std::memory_order_seq_cst);
    };

    const TimingObservation sequential_scheduler =
        measure_plan_total(sequential, overhead_iterations, overhead_runs, scheduler_function);

    double overhead_base = 0.0;
    double overhead_slope = 0.0;
    double overhead_uncertainty = sequential_scheduler.relative_mad;
    if (strategy == ExecutionStrategy::DynamicChunks)
    {
        const std::size_t low_chunk_count = std::max<std::size_t>(workers * 2, 2);
        const std::size_t high_chunk_count =
            std::max<std::size_t>(workers * 32, low_chunk_count + 1);

        const std::size_t low_chunk = chunk_for_count(overhead_iterations, low_chunk_count);
        const std::size_t high_chunk = chunk_for_count(overhead_iterations, high_chunk_count);
        const TimingObservation low =
            measure_plan_total(calibration_plan(engine, strategy, workers, low_chunk),
                               overhead_iterations,
                               overhead_runs,
                               scheduler_function);
        const TimingObservation high =
            measure_plan_total(calibration_plan(engine, strategy, workers, high_chunk),
                               overhead_iterations,
                               overhead_runs,
                               scheduler_function);

        const double ideal_sequential =
            sequential_scheduler.median_ms / static_cast<double>(workers);

        const double low_overhead = std::max(0.0, low.median_ms - ideal_sequential);
        const double high_overhead = std::max(low_overhead, high.median_ms - ideal_sequential);

        const double low_log = std::log2(static_cast<double>(low_chunk_count) + 1.0);
        const double high_log = std::log2(static_cast<double>(high_chunk_count) + 1.0);
        overhead_slope =
            std::max(0.0, (high_overhead - low_overhead) / std::max(1.0e-9, high_log - low_log));
        overhead_base = std::max(0.0, low_overhead - overhead_slope * low_log);
        overhead_uncertainty = std::clamp(
            std::max({sequential_scheduler.relative_mad, low.relative_mad, high.relative_mad}),
            0.0,
            2.0);
    }
    else
    {
        const TimingObservation parallel_scheduler =
            measure_plan_total(calibration_plan(engine, strategy, workers, 0),
                               overhead_iterations,
                               overhead_runs,
                               scheduler_function);
        overhead_base =
            std::max(0.0,
                     parallel_scheduler.median_ms
                         - sequential_scheduler.median_ms / static_cast<double>(workers));
        overhead_slope = 0.0;
        overhead_uncertainty = std::clamp(
            std::max(sequential_scheduler.relative_mad, parallel_scheduler.relative_mad), 0.0, 2.0);
    }

    std::vector<std::uint64_t> cheap_values(cheap_large_iterations, 1u);
    const auto cheap_function = [&](std::size_t index)
    {
        cheap_values[index] = cheap_values[index] * 3u + 7u;
    };
    const auto cheap_small = measure_useful_speedup(engine,
                                                    strategy,
                                                    workers,
                                                    cheap_small_iterations,
                                                    throughput_runs,
                                                    overhead_base,
                                                    overhead_slope,
                                                    cheap_function);
    const auto cheap_large = measure_useful_speedup(engine,
                                                    strategy,
                                                    workers,
                                                    cheap_large_iterations,
                                                    throughput_runs,
                                                    overhead_base,
                                                    overhead_slope,
                                                    cheap_function);
    runtime_calibration_sink ^= cheap_values[cheap_values.size() / 2];

    std::vector<std::uint64_t> compute_values(compute_iterations, 0u);
    const auto compute_function = [&](std::size_t index)
    {
        std::uint64_t value = static_cast<std::uint64_t>(index + 1);
        for (int step = 0; step < 96; ++step)
        {
            value = value * 6364136223846793005ull + 1442695040888963407ull;
            value ^= value >> 17;
        }
        compute_values[index] = value;
    };
    const auto compute = measure_useful_speedup(engine,
                                                strategy,
                                                workers,
                                                compute_iterations,
                                                throughput_runs,
                                                overhead_base,
                                                overhead_slope,
                                                compute_function);
    runtime_calibration_sink ^= compute_values[compute_values.size() / 2];

    std::vector<std::uint64_t> stream_values(stream_iterations, 1u);
    const auto stream_function = [&](std::size_t index)
    {
        stream_values[index] = stream_values[index] * 3u + 1u;
    };
    const auto stream = measure_useful_speedup(engine,
                                               strategy,
                                               workers,
                                               stream_iterations,
                                               throughput_runs,
                                               overhead_base,
                                               overhead_slope,
                                               stream_function);
    runtime_calibration_sink ^= stream_values[stream_values.size() / 2];

    BackendRuntimeCalibrationPoint point;
    point.engine = engine;
    point.workers = workers;
    point.base_overhead_ms = overhead_base;
    point.log_slope_ms = overhead_slope;
    point.overhead_relative_uncertainty = overhead_uncertainty;
    point.cheap_small_speedup = cheap_small.speedup;
    point.cheap_small_confidence = cheap_small.confidence;
    point.cheap_large_speedup = cheap_large.speedup;
    point.cheap_large_confidence = cheap_large.confidence;
    point.compute_speedup = compute.speedup;
    point.compute_confidence = compute.confidence;
    point.streaming_speedup = stream.speedup;
    point.streaming_confidence = stream.confidence;
    point.speedup_relative_uncertainty = std::clamp(std::max({cheap_small.relative_uncertainty,
                                                              cheap_large.relative_uncertainty,
                                                              compute.relative_uncertainty,
                                                              stream.relative_uncertainty}),
                                                    0.0,
                                                    2.0);
    return point;
}

inline BackendRuntimeCalibration measure_backend_calibration()
{
    BackendRuntimeCalibration calibration;
    const std::size_t maximum_workers = std::max<std::size_t>(1, hardware_threads());
    if (maximum_workers <= 1)
        return calibration;

    const std::vector<std::size_t> worker_counts = calibration_worker_counts(maximum_workers);
    calibration.points.reserve(worker_counts.size() * 3);
    for (std::size_t workers : worker_counts)
    {
        calibration.points.push_back(measure_point(
            ExecutionEngineType::ThreadPool, ExecutionStrategy::DynamicChunks, workers));
        calibration.points.push_back(measure_point(
            ExecutionEngineType::StaticThread, ExecutionStrategy::StaticChunks, workers));
        calibration.points.push_back(
            measure_point(ExecutionEngineType::OneTbb, ExecutionStrategy::DynamicChunks, workers));
    }
    calibration.measured = !calibration.points.empty();
    return calibration;
}

inline const BackendRuntimeCalibrationPoint* closest_point(
    const BackendRuntimeCalibration& calibration, ExecutionEngineType engine, std::size_t workers)
{
    const BackendRuntimeCalibrationPoint* selected = nullptr;
    std::size_t selected_distance = std::numeric_limits<std::size_t>::max();
    for (const BackendRuntimeCalibrationPoint& point : calibration.points)
    {
        if (point.engine != engine)
            continue;
        const std::size_t distance =
            point.workers > workers ? point.workers - workers : workers - point.workers;
        if (selected == nullptr || distance < selected_distance)
        {
            selected = &point;
            selected_distance = distance;
        }
    }
    return selected;
}

inline double smoothstep(double low, double high, double value)
{
    if (!(high > low))
        return value >= high ? 1.0 : 0.0;
    const double t = std::clamp((value - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

inline double geometric_blend(double left, double right, double weight)
{
    const double safe_left = std::max(1.0e-6, left);
    const double safe_right = std::max(1.0e-6, right);
    const double t = std::clamp(weight, 0.0, 1.0);
    return std::exp((1.0 - t) * std::log(safe_left) + t * std::log(safe_right));
}
} // namespace detail

inline const BackendRuntimeCalibration& backend_runtime_calibration()
{
    static const BackendRuntimeCalibration calibration = detail::measure_backend_calibration();
    return calibration;
}

inline double calibrated_backend_overhead_ms(ExecutionEngineType engine,
                                             std::size_t iterations,
                                             std::size_t workers,
                                             std::size_t chunk_size)
{
    const BackendRuntimeCalibration& calibration = backend_runtime_calibration();
    const BackendRuntimeCalibrationPoint* point =
        detail::closest_point(calibration, engine, workers);
    if (point == nullptr)
        return 0.0;

    std::size_t total_chunks = workers;
    if (chunk_size > 0 && engine != ExecutionEngineType::StaticThread)
    {
        total_chunks = (iterations + chunk_size - 1) / chunk_size;
    }
    return detail::overhead_from_curve(point->base_overhead_ms, point->log_slope_ms, total_chunks);
}

inline double calibrated_backend_speedup(ExecutionEngineType engine,
                                         std::size_t workers,
                                         const RuntimeCalibrationQuery& query)
{
    const BackendRuntimeCalibration& calibration = backend_runtime_calibration();
    const BackendRuntimeCalibrationPoint* point =
        detail::closest_point(calibration, engine, workers);
    if (point == nullptr)
        return 1.0;

    const double size_position = detail::smoothstep(
        std::log2(65'536.0),
        std::log2(1'048'576.0),
        std::log2(static_cast<double>(std::max<std::size_t>(1, query.iterations))));
    const double cheap_speedup = detail::geometric_blend(
        point->cheap_small_speedup, point->cheap_large_speedup, size_position);

    // Cost is converted from milliseconds to microseconds. The blend is
    // deliberately smooth: hard General/Compute/Streaming buckets caused
    // abrupt backend crossovers near cheap and cache-resident workloads.
    const double cost_us = std::max(1.0e-6, query.per_iteration_ms * 1.0e3);
    const double compute_weight =
        detail::smoothstep(std::log2(0.08), std::log2(2.0), std::log2(cost_us));
    const double compute_like =
        detail::geometric_blend(cheap_speedup, point->compute_speedup, compute_weight);
    return std::clamp(detail::geometric_blend(compute_like,
                                              point->streaming_speedup,
                                              std::clamp(query.streaming_strength, 0.0, 1.0)),
                      1.0,
                      std::max(1.0, static_cast<double>(workers) * 1.25));
}

inline double calibrated_backend_speedup_authority(ExecutionEngineType engine,
                                                   std::size_t workers,
                                                   const RuntimeCalibrationQuery& query)
{
    const BackendRuntimeCalibrationPoint* point =
        detail::closest_point(backend_runtime_calibration(), engine, workers);
    if (point == nullptr)
        return 0.0;

    const double size_position = detail::smoothstep(
        std::log2(65'536.0),
        std::log2(1'048'576.0),
        std::log2(static_cast<double>(std::max<std::size_t>(1, query.iterations))));

    const double cheap_confidence = (1.0 - size_position) * point->cheap_small_confidence
                                    + size_position * point->cheap_large_confidence;

    const double cost_us = std::max(1.0e-6, query.per_iteration_ms * 1.0e3);
    const double compute_weight =
        detail::smoothstep(std::log2(0.08), std::log2(2.0), std::log2(cost_us));
    const double compute_like_confidence =
        (1.0 - compute_weight) * cheap_confidence + compute_weight * point->compute_confidence;
    const double streaming = std::clamp(query.streaming_strength, 0.0, 1.0);
    const double probe_confidence =
        (1.0 - streaming) * compute_like_confidence + streaming * point->streaming_confidence;

    // The synthetic surface contains uniform cheap, compute, and
    // contiguous streaming kernels. Irregular dependencies, branches,
    // and large records are outside that domain and must fall back toward
    // the analytical model rather than inheriting a synthetic backend
    // crossover.
    const double out_of_domain = std::clamp(std::max({query.irregular_strength,
                                                      0.80 * query.branch_strength,
                                                      0.90 * query.large_record_strength}),
                                            0.0,
                                            1.0);
    const double domain_scale =
        (1.0 - out_of_domain)
        + out_of_domain * global_config().machine_calibration_out_of_domain_scale;
    return std::clamp(probe_confidence * domain_scale, 0.0, 1.0);
}

inline double calibrated_backend_speedup(ExecutionEngineType engine,
                                         std::size_t workers,
                                         RuntimeWorkloadClass workload_class)
{
    RuntimeCalibrationQuery query;
    query.iterations = workload_class == RuntimeWorkloadClass::ComputeLike ? 32'768 : 1'048'576;
    query.per_iteration_ms = workload_class == RuntimeWorkloadClass::ComputeLike ? 0.002 : 0.00005;
    query.streaming_strength = workload_class == RuntimeWorkloadClass::StreamingLike ? 1.0
                               : workload_class == RuntimeWorkloadClass::General     ? 0.35
                                                                                     : 0.0;
    return calibrated_backend_speedup(engine, workers, query);
}

inline double calibrated_backend_relative_uncertainty(ExecutionEngineType engine,
                                                      std::size_t workers)
{
    const BackendRuntimeCalibrationPoint* point =
        detail::closest_point(backend_runtime_calibration(), engine, workers);
    if (point == nullptr)
        return 0.35;
    return std::clamp(
        std::max(point->overhead_relative_uncertainty, point->speedup_relative_uncertainty),
        0.02,
        1.0);
}
} // namespace smart
