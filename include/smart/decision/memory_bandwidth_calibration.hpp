#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <smart/decision/plan_prediction.hpp>
#include <smart/model/performance_model.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_family.hpp>

namespace smart
{
    struct MemoryBandwidthCalibrationResult
    {
        double factor = 1.0;
        double saturation_strength = 0.0;
        double worker_excess = 0.0;
        double bytes_per_iteration = 0.0;
        std::size_t estimated_saturation_workers = 1;
        bool large_record_stream = false;
        bool applied = false;
    };

    // Step 1 decision-quality refinement.
    //
    // Contiguous, cache-exceeding streams normally stop scaling before all
    // logical CPUs are occupied. The analytical model previously treated
    // extra workers too optimistically, which made high-worker plans look
    // cheaper than they are for memory_stream and large-record workloads.
    // This policy adds a bounded, hardware-relative saturation correction.
    class MemoryBandwidthCalibrationPolicy
    {
    public:
        MemoryBandwidthCalibrationResult evaluate(
            const ExecutionPlan& plan,
            const WorkloadFamilyClassification& classification,
            const WorkloadAnalysis& analysis,
            const PerformanceModel& model) const
        {
            MemoryBandwidthCalibrationResult result;

            if (classification.family != WorkloadFamily::StreamingMemory ||
                classification.confidence <= 0.0 ||
                analysis.structural.logical_iterations == 0)
            {
                return result;
            }

            const double confidence = std::clamp(
                classification.confidence,
                0.0,
                1.0);
            const double l3_pressure = std::max(
                0.0,
                analysis.structural.cache_ratios_available
                    ? analysis.structural.l3_residency_ratio
                    : model.l3_pressure);

            result.bytes_per_iteration =
                static_cast<double>(analysis.structural.represented_input_bytes) /
                static_cast<double>(analysis.structural.logical_iterations);
            result.large_record_stream = analysis.objects_are_large ||
                result.bytes_per_iteration >= 128.0;

            const double pressure_strength = std::clamp(
                std::log2(std::max(1.0, l3_pressure)) / 5.0,
                0.0,
                1.0);
            const double record_strength = std::clamp(
                std::log2(std::max(1.0, result.bytes_per_iteration / 64.0)) / 4.0,
                0.0,
                1.0);
            result.saturation_strength = std::clamp(
                0.72 * pressure_strength + 0.28 * record_strength,
                0.0,
                1.0);

            const std::size_t logical = std::max<std::size_t>(
                1,
                model.hardware.logical_threads);
            const std::size_t physical = std::max<std::size_t>(
                1,
                model.hardware.physical_cores == 0
                    ? logical
                    : model.hardware.physical_cores);

            // High-pressure streams generally saturate around one third to
            // one half of the physical cores. Lower-pressure streams retain a
            // wider scaling window. Never estimate fewer than two workers.
            const double saturation_fraction =
                0.78 - 0.43 * result.saturation_strength;
            result.estimated_saturation_workers = std::max<std::size_t>(
                2,
                static_cast<std::size_t>(std::ceil(
                    static_cast<double>(physical) * saturation_fraction)));
            result.estimated_saturation_workers = std::min(
                result.estimated_saturation_workers,
                logical);

            double family_factor = 1.0;
            if (plan.parallel)
            {
                const std::size_t workers = std::max<std::size_t>(1, plan.job_count);
                if (workers > result.estimated_saturation_workers)
                {
                    result.worker_excess = std::clamp(
                        static_cast<double>(
                            workers - result.estimated_saturation_workers) /
                            static_cast<double>(std::max<std::size_t>(
                                1,
                                logical - result.estimated_saturation_workers)),
                        0.0,
                        1.0);
                }

                family_factor +=
                    0.06 * result.saturation_strength +
                    0.42 * result.worker_excess *
                        (0.45 + 0.55 * result.saturation_strength);

                if (result.large_record_stream)
                {
                    const double worker_pressure = std::clamp(
                        static_cast<double>(workers) /
                            static_cast<double>(physical),
                        0.0,
                        2.0);
                    family_factor += 0.08 * record_strength * worker_pressure;
                }

                // Static partitioning cannot adapt when bandwidth or page
                // pressure differs across worker regions. oneTBB receives a
                // small discount because its partitioner is more adaptive.
                if (plan.engine == ExecutionEngineType::StaticThread)
                    family_factor += 0.035 * result.saturation_strength;
                else if (plan.engine == ExecutionEngineType::OneTbb)
                    family_factor -= 0.015 * result.saturation_strength;
            }
            else if (analysis.structural.logical_iterations >= 100'000 &&
                     result.saturation_strength < 0.35)
            {
                // Do not let the saturation correction turn every stream into
                // a sequential workload. Mildly penalize sequential execution
                // while useful parallel bandwidth is still available.
                family_factor += 0.035 * (0.35 - result.saturation_strength);
            }

            family_factor = std::clamp(family_factor, 0.96, 1.55);
            result.factor = 1.0 + (family_factor - 1.0) * confidence;
            result.factor = std::clamp(result.factor, 0.97, 1.48);
            result.applied = std::abs(result.factor - 1.0) > 1.0e-9;
            return result;
        }

        MemoryBandwidthCalibrationResult apply(
            PlanCostEstimate& estimate,
            const WorkloadFamilyClassification& classification,
            const WorkloadAnalysis& analysis,
            const PerformanceModel& model) const
        {
            const MemoryBandwidthCalibrationResult result = evaluate(
                estimate.plan,
                classification,
                analysis,
                model);

            estimate.memory_bandwidth_calibration_applied = result.applied;
            estimate.memory_bandwidth_calibration_factor = result.factor;
            estimate.memory_bandwidth_saturation_strength =
                result.saturation_strength;
            estimate.memory_bandwidth_worker_excess = result.worker_excess;
            estimate.memory_bandwidth_bytes_per_iteration =
                result.bytes_per_iteration;
            estimate.memory_bandwidth_saturation_workers =
                result.estimated_saturation_workers;

            if (!result.applied)
                return result;

            estimate.predicted_execution_ms *= result.factor;
            estimate.memory_penalty_ms *= result.factor;
            estimate.predicted_total_ms =
                estimate.predicted_execution_ms +
                estimate.imbalance_penalty_ms +
                estimate.scheduling_overhead_ms +
                estimate.framework_overhead_ms;
            estimate.predicted_speedup = estimate.predicted_execution_ms > 0.0
                ? std::max(1.0, estimate.useful_work_ms /
                    estimate.predicted_execution_ms)
                : estimate.predicted_speedup;
            return result;
        }
    };
}
