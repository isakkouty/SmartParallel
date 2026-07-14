#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <smart/core/config.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/execution/executor.hpp>
#include <smart/hardware/hardware.hpp>
#include <smart/workload/workload.hpp>

namespace smart
{
    enum class RuntimeWorkloadClass
    {
        General,
        ComputeLike,
        StreamingLike
    };

    struct BackendRuntimeCalibrationPoint
    {
        ExecutionEngineType engine = ExecutionEngineType::ThreadPool;
        std::size_t workers = 1;
        double base_overhead_ms = 0.03;
        double log_slope_ms = 0.002;
        double compute_speedup = 1.0;
        double streaming_speedup = 1.0;
    };

    struct BackendRuntimeCalibration
    {
        std::vector<BackendRuntimeCalibrationPoint> points;
        bool measured = false;
    };

    namespace detail
    {
        inline double calibration_now_ms()
        {
            const auto now = std::chrono::steady_clock::now();
            return std::chrono::duration<double, std::milli>(
                now.time_since_epoch()).count();
        }

        inline double median(std::vector<double> values)
        {
            if (values.empty())
            {
                return 0.0;
            }

            std::sort(values.begin(), values.end());
            return values[values.size() / 2];
        }

        inline ExecutionPlan calibration_plan(
            ExecutionEngineType engine,
            ExecutionStrategy strategy,
            std::size_t workers,
            std::size_t chunk_size = 0)
        {
            ExecutionPlan plan;
            plan.engine = engine;
            plan.strategy = strategy;
            plan.parallel = strategy != ExecutionStrategy::Sequential;
            plan.job_count = plan.parallel ? std::max<std::size_t>(1, workers) : 1;
            plan.chunk_size = strategy == ExecutionStrategy::DynamicChunks
                ? std::max<std::size_t>(1, chunk_size)
                : 0;
            return plan;
        }

        template <typename Function>
        inline double measure_plan_total(
            const ExecutionPlan& plan,
            std::size_t iterations,
            int runs,
            Function function)
        {
            Workload workload;
            workload.iterations = iterations;

            execute_workload(workload, plan, function);

            std::vector<double> samples;
            samples.reserve(static_cast<std::size_t>(runs));

            for (int run = 0; run < runs; ++run)
            {
                const double start = calibration_now_ms();
                execute_workload(workload, plan, function);
                samples.push_back(calibration_now_ms() - start);
            }

            return median(std::move(samples));
        }

        inline std::vector<std::size_t> calibration_worker_counts(
            std::size_t maximum_workers)
        {
            std::vector<std::size_t> counts;
            if (maximum_workers <= 1)
            {
                return counts;
            }

            for (std::size_t workers = 2; workers < maximum_workers;)
            {
                counts.push_back(workers);
                if (workers > maximum_workers / 2)
                {
                    break;
                }
                workers *= 2;
            }

            if (counts.empty() || counts.back() != maximum_workers)
            {
                counts.push_back(maximum_workers);
            }

            counts.erase(std::unique(counts.begin(), counts.end()), counts.end());
            return counts;
        }

        inline double safe_speedup(double sequential_ms, double parallel_ms)
        {
            if (!std::isfinite(sequential_ms) ||
                !std::isfinite(parallel_ms) ||
                sequential_ms <= 0.0 ||
                parallel_ms <= 0.0)
            {
                return 1.0;
            }

            return std::clamp(sequential_ms / parallel_ms, 1.0, 128.0);
        }

        inline BackendRuntimeCalibrationPoint measure_point(
            ExecutionEngineType engine,
            ExecutionStrategy strategy,
            std::size_t workers)
        {
            constexpr std::size_t small_iterations = 128;
            constexpr std::size_t large_iterations = 16'384;
            constexpr std::size_t compute_iterations = 32'768;
            constexpr std::size_t stream_iterations = 1'048'576;
            constexpr int overhead_runs = 5;
            constexpr int throughput_runs = 3;

            const ExecutionPlan sequential = calibration_plan(
                ExecutionEngineType::ThreadPool,
                ExecutionStrategy::Sequential,
                1);

            const std::size_t dynamic_grain = std::max<std::size_t>(
                1,
                large_iterations / std::max<std::size_t>(1, workers * 16));
            const ExecutionPlan parallel = calibration_plan(
                engine,
                strategy,
                workers,
                dynamic_grain);

            std::vector<std::uint64_t> dispatch_values(large_iterations, 0u);
            const auto dispatch_function = [&](std::size_t index)
            {
                dispatch_values[index] += static_cast<std::uint64_t>(index + 1);
            };

            const double sequential_small = measure_plan_total(
                sequential,
                small_iterations,
                overhead_runs,
                dispatch_function);
            const double sequential_large = measure_plan_total(
                sequential,
                large_iterations,
                overhead_runs,
                dispatch_function);
            const double parallel_small = measure_plan_total(
                parallel,
                small_iterations,
                overhead_runs,
                dispatch_function);
            const double parallel_large = measure_plan_total(
                parallel,
                large_iterations,
                overhead_runs,
                dispatch_function);

            const double small_overhead = std::max(
                0.0,
                parallel_small - sequential_small / static_cast<double>(workers));
            const double large_overhead = std::max(
                small_overhead,
                parallel_large - sequential_large / static_cast<double>(workers));

            const double small_log = std::log2(
                static_cast<double>(small_iterations) + 1.0);
            const double large_log = std::log2(
                static_cast<double>(large_iterations) + 1.0);
            const double slope = std::max(
                0.0,
                (large_overhead - small_overhead) /
                    std::max(1.0, large_log - small_log));
            const double base = std::max(
                0.0,
                small_overhead - slope * small_log);

            std::vector<std::uint64_t> compute_values(compute_iterations, 0u);
            const auto compute_function = [&](std::size_t index)
            {
                std::uint64_t value = static_cast<std::uint64_t>(index + 1);
                for (int step = 0; step < 96; ++step)
                {
                    value = value * 6364136223846793005ull +
                        1442695040888963407ull;
                    value ^= value >> 17;
                }
                compute_values[index] = value;
            };

            const double sequential_compute = measure_plan_total(
                sequential,
                compute_iterations,
                throughput_runs,
                compute_function);
            const double parallel_compute = measure_plan_total(
                parallel,
                compute_iterations,
                throughput_runs,
                compute_function);

            std::vector<std::uint64_t> stream_values(stream_iterations, 1u);
            const auto stream_function = [&](std::size_t index)
            {
                stream_values[index] = stream_values[index] * 3u + 1u;
            };

            const double sequential_stream = measure_plan_total(
                sequential,
                stream_iterations,
                throughput_runs,
                stream_function);
            const double parallel_stream = measure_plan_total(
                parallel,
                stream_iterations,
                throughput_runs,
                stream_function);

            BackendRuntimeCalibrationPoint point;
            point.engine = engine;
            point.workers = workers;
            point.base_overhead_ms = base;
            point.log_slope_ms = slope;
            point.compute_speedup = safe_speedup(
                sequential_compute,
                parallel_compute);
            point.streaming_speedup = safe_speedup(
                sequential_stream,
                parallel_stream);
            return point;
        }

        inline BackendRuntimeCalibration measure_backend_calibration()
        {
            BackendRuntimeCalibration calibration;

            const std::size_t maximum_workers = std::max<std::size_t>(
                1,
                hardware_threads());
            if (maximum_workers <= 1)
            {
                return calibration;
            }

            const std::vector<std::size_t> worker_counts =
                calibration_worker_counts(maximum_workers);
            calibration.points.reserve(worker_counts.size() * 3);

            for (std::size_t workers : worker_counts)
            {
                calibration.points.push_back(measure_point(
                    ExecutionEngineType::ThreadPool,
                    ExecutionStrategy::DynamicChunks,
                    workers));
                calibration.points.push_back(measure_point(
                    ExecutionEngineType::StaticThread,
                    ExecutionStrategy::StaticChunks,
                    workers));
                calibration.points.push_back(measure_point(
                    ExecutionEngineType::OneTbb,
                    ExecutionStrategy::DynamicChunks,
                    workers));
            }

            calibration.measured = !calibration.points.empty();
            return calibration;
        }

        inline const BackendRuntimeCalibrationPoint* closest_point(
            const BackendRuntimeCalibration& calibration,
            ExecutionEngineType engine,
            std::size_t workers)
        {
            const BackendRuntimeCalibrationPoint* selected = nullptr;
            std::size_t selected_distance = std::numeric_limits<std::size_t>::max();

            for (const BackendRuntimeCalibrationPoint& point : calibration.points)
            {
                if (point.engine != engine)
                {
                    continue;
                }

                const std::size_t distance = point.workers > workers
                    ? point.workers - workers
                    : workers - point.workers;
                if (selected == nullptr || distance < selected_distance)
                {
                    selected = &point;
                    selected_distance = distance;
                }
            }

            return selected;
        }
    }

    inline const BackendRuntimeCalibration& backend_runtime_calibration()
    {
        static const BackendRuntimeCalibration calibration =
            detail::measure_backend_calibration();
        return calibration;
    }

    inline double calibrated_backend_overhead_ms(
        ExecutionEngineType engine,
        std::size_t iterations,
        std::size_t workers,
        std::size_t chunk_size)
    {
        const BackendRuntimeCalibration& calibration =
            backend_runtime_calibration();
        const BackendRuntimeCalibrationPoint* point =
            detail::closest_point(calibration, engine, workers);
        if (point == nullptr)
        {
            return 0.0;
        }

        const double log_scale = std::log2(
            static_cast<double>(std::max<std::size_t>(1, iterations)) + 1.0);
        double overhead = point->base_overhead_ms +
            point->log_slope_ms * log_scale;

        if (chunk_size > 0 && engine != ExecutionEngineType::StaticThread)
        {
            const double chunks = static_cast<double>(
                (iterations + chunk_size - 1) / chunk_size);
            overhead *= 1.0 + 0.01 * std::log2(chunks + 1.0);
        }

        return std::max(0.0, overhead);
    }

    inline double calibrated_backend_speedup(
        ExecutionEngineType engine,
        std::size_t workers,
        RuntimeWorkloadClass workload_class)
    {
        const BackendRuntimeCalibration& calibration =
            backend_runtime_calibration();
        const BackendRuntimeCalibrationPoint* point =
            detail::closest_point(calibration, engine, workers);
        if (point == nullptr)
        {
            return 1.0;
        }

        switch (workload_class)
        {
        case RuntimeWorkloadClass::StreamingLike:
            return point->streaming_speedup;
        case RuntimeWorkloadClass::ComputeLike:
            return point->compute_speedup;
        case RuntimeWorkloadClass::General:
            return std::sqrt(
                std::max(1.0, point->compute_speedup) *
                std::max(1.0, point->streaming_speedup));
        }

        return 1.0;
    }
}
