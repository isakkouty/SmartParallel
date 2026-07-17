#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include <smart/decision/execution_hints.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/model/performance_model.hpp>
#include <smart/profiling/function_profiler.hpp>
#include <smart/workload/workload.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_family.hpp>

namespace smart
{
    inline constexpr std::size_t residual_feature_count = 26;

    struct ResidualFeatureVector
    {
        std::array<double, residual_feature_count> values{};
        WorkloadFamilyMembership family_membership{};
        double feature_confidence = 0.0;
        bool available = false;
    };

    class ResidualFeatureBuilder
    {
    public:
        ResidualFeatureVector build(
            const Workload& workload,
            const WorkloadAnalysis& analysis,
            const FunctionProfile& profile,
            const ExecutionHints* hints,
            const WorkloadFamilyClassification& classification,
            const PerformanceModel& model,
            const ExecutionPlan& plan,
            double useful_work_ms,
            double predicted_execution_ms,
            double scheduling_overhead_ms) const
        {
            ResidualFeatureVector result;
            if (workload.iterations == 0 || !profile.available)
                return result;

            const double iterations = static_cast<double>(workload.iterations);
            const double physical = static_cast<double>(
                std::max<std::size_t>(
                    1,
                    model.hardware.physical_cores == 0
                        ? model.hardware.logical_threads
                        : model.hardware.physical_cores));
            const double logical = static_cast<double>(
                std::max<std::size_t>(1, model.hardware.logical_threads));
            const double workers = plan.parallel
                ? static_cast<double>(std::max<std::size_t>(1, plan.job_count))
                : 1.0;
            const double chunk = plan.strategy == ExecutionStrategy::DynamicChunks
                ? static_cast<double>(std::max<std::size_t>(1, plan.chunk_size))
                : std::max(1.0, iterations / workers);
            const double chunks_per_worker =
                iterations / std::max(1.0, chunk * workers);
            const double represented_bytes = static_cast<double>(
                analysis.structural.represented_input_bytes);
            const double effective_working_set = represented_bytes +
                static_cast<double>(
                    hints != nullptr && hints->available
                        ? hints->external_working_set_bytes
                        : 0);
            const double l3_bytes = static_cast<double>(
                std::max<std::size_t>(1, model.hardware.l3_cache_size));
            const double l3_ratio = effective_working_set / l3_bytes;
            const double bytes_per_iteration =
                hints != nullptr && hints->available &&
                    hints->bytes_touched_per_iteration > 0.0
                    ? hints->bytes_touched_per_iteration
                    : represented_bytes / iterations;
            const double per_iteration_ms = useful_work_ms /
                std::max(1.0, iterations);
            const double overhead_fraction = scheduling_overhead_ms /
                std::max(1.0e-9, predicted_execution_ms + scheduling_overhead_ms);

            auto& x = result.values;
            x[0] = 1.0;
            x[1] = log_unit(iterations, 24.0);
            x[2] = signed_log_unit(useful_work_ms, 6.0);
            x[3] = signed_log_unit(per_iteration_ms * 1.0e6, 8.0);
            x[4] = clamp(profile.coefficient_of_variation / 2.0, 0.0, 1.0);
            x[5] = clamp((std::max(1.0, profile.tail_ratio) - 1.0) / 4.0, 0.0, 1.0);
            x[6] = clamp((std::max(1.0, profile.regional_cost_ratio) - 1.0) / 4.0, 0.0, 1.0);
            x[7] = signed_log2_ratio(l3_ratio, 4.0);
            x[8] = signed_log_unit(bytes_per_iteration, 6.0);
            x[9] = log_unit(workers, 6.0);
            x[10] = clamp(workers / physical, 0.0, 2.0) / 2.0;
            x[11] = clamp(workers / logical, 0.0, 1.5) / 1.5;
            x[12] = log_unit(chunk, 16.0);
            x[13] = log_unit(chunks_per_worker, 12.0);
            x[14] = plan.strategy == ExecutionStrategy::DynamicChunks ? 1.0 : 0.0;
            x[15] = plan.strategy == ExecutionStrategy::StaticChunks ? 1.0 : 0.0;
            x[16] = !plan.parallel || plan.strategy == ExecutionStrategy::Sequential
                ? 1.0
                : 0.0;
            x[17] = plan.parallel && plan.engine == ExecutionEngineType::ThreadPool
                ? 1.0
                : 0.0;
            x[18] = plan.parallel && plan.engine == ExecutionEngineType::StaticThread
                ? 1.0
                : 0.0;
            x[19] = plan.parallel && plan.engine == ExecutionEngineType::OneTbb
                ? 1.0
                : 0.0;
            x[20] = hint_value(hints, &ExecutionHints::memory_randomness);
            x[21] = dependency_strength(hints);
            x[22] = hint_value(hints, &ExecutionHints::arithmetic_intensity);
            x[23] = hint_value(hints, &ExecutionHints::branchiness);
            x[24] = hint_value(hints, &ExecutionHints::vectorization_potential);
            x[25] = clamp(overhead_fraction, 0.0, 1.0);

            result.family_membership = classification.membership;
            const double profile_confidence = profile.measurement_reliable
                ? clamp(static_cast<double>(profile.measured_batches) / 12.0, 0.25, 1.0)
                : 0.30;
            const double structural_confidence =
                analysis.structural.cache_ratios_available ? 1.0 : 0.70;
            const double hint_confidence = hints != nullptr && hints->available
                ? clamp(hints->feature_confidence, 0.0, 1.0)
                : 0.70;
            result.feature_confidence = clamp(
                0.45 * profile_confidence +
                0.25 * structural_confidence +
                0.20 * std::max(0.25, classification.confidence) +
                0.10 * hint_confidence,
                0.0,
                1.0);
            result.available = true;
            return result;
        }

    private:
        static double clamp(double value, double low, double high)
        {
            if (!std::isfinite(value))
                return low;
            return std::max(low, std::min(high, value));
        }

        static double log_unit(double value, double denominator)
        {
            return clamp(
                std::log2(std::max(1.0, value)) /
                    std::max(1.0, denominator),
                0.0,
                1.5);
        }

        static double signed_log_unit(double value, double scale)
        {
            if (!std::isfinite(value) || value <= 0.0)
                return 0.0;
            return std::tanh(std::log1p(value) / std::max(1.0, scale));
        }

        static double signed_log2_ratio(double value, double scale)
        {
            if (!std::isfinite(value) || value <= 0.0)
                return -1.0;
            return clamp(
                std::log2(value) / std::max(1.0, scale),
                -1.0,
                1.0);
        }

        using HintMember = double ExecutionHints::*;

        static double hint_value(
            const ExecutionHints* hints,
            HintMember member)
        {
            if (hints == nullptr || !hints->available)
                return 0.0;
            return clamp(hints->*member, 0.0, 1.0);
        }

        static double dependency_strength(const ExecutionHints* hints)
        {
            if (hints == nullptr || !hints->available)
                return 0.0;
            const double accesses = std::max(
                0.0, hints->dependent_memory_accesses_per_iteration);
            const double depth = std::max(0.0, hints->dependency_depth);
            const double mlp = hints->estimated_memory_level_parallelism > 0.0
                ? hints->estimated_memory_level_parallelism
                : 1.0;
            return clamp(
                std::log1p(accesses + depth) /
                    std::log(9.0) /
                    std::max(1.0, std::sqrt(mlp)),
                0.0,
                1.0);
        }
    };
}
