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
    struct FamilyCalibrationResult
    {
        double factor = 1.0;
        double confidence_weight = 0.0;
        double memory_saturation = 0.0;
        bool applied = false;
    };

    class FamilyCalibrationPolicy
    {
    public:
        FamilyCalibrationResult evaluate(
            const ExecutionPlan& plan,
            const WorkloadFamilyClassification& classification,
            const WorkloadAnalysis& analysis,
            const PerformanceModel& model) const
        {
            FamilyCalibrationResult result;
            result.confidence_weight = std::clamp(
                classification.confidence,
                0.0,
                1.0);

            if (classification.family == WorkloadFamily::Unknown ||
                result.confidence_weight <= 0.0)
            {
                return result;
            }

            const double pressure = std::max(
                0.0,
                analysis.structural.cache_ratios_available
                    ? analysis.structural.l3_residency_ratio
                    : model.l3_pressure);
            result.memory_saturation = std::clamp(
                pressure <= 1.0 ? 0.0 : std::log2(pressure + 1.0) / 6.0,
                0.0,
                1.0);

            const double workers = static_cast<double>(
                std::max<std::size_t>(1, plan.job_count));
            const double worker_pressure = std::clamp(
                std::log2(workers) / 5.0,
                0.0,
                1.0);

            double family_factor = 1.0;
            switch (classification.family)
            {
            case WorkloadFamily::ComputeHeavy:
                if (plan.parallel)
                {
                    family_factor -= 0.08 * worker_pressure;
                    if (plan.engine == ExecutionEngineType::StaticThread)
                        family_factor -= 0.015;
                }
                else
                {
                    family_factor += 0.04;
                }
                break;

            case WorkloadFamily::StreamingMemory:
                if (plan.parallel)
                {
                    family_factor +=
                        result.memory_saturation * (0.05 + 0.15 * worker_pressure);
                    if (plan.engine == ExecutionEngineType::StaticThread)
                        family_factor += 0.04;
                    if (plan.engine == ExecutionEngineType::OneTbb)
                        family_factor -= 0.015;
                }
                else if (analysis.structural.logical_iterations >= 50'000)
                {
                    family_factor += 0.06;
                }
                break;

            case WorkloadFamily::IrregularMemory:
                if (plan.strategy == ExecutionStrategy::StaticChunks)
                    family_factor += 0.12;
                else if (plan.parallel)
                    family_factor -= 0.04;

                if (plan.parallel)
                    family_factor += 0.05 * worker_pressure;
                break;

            case WorkloadFamily::BranchHeavy:
                if (plan.strategy == ExecutionStrategy::DynamicChunks)
                    family_factor -= 0.05;
                else if (plan.strategy == ExecutionStrategy::StaticChunks)
                    family_factor += 0.04;
                break;

            case WorkloadFamily::Mixed:
                if (plan.strategy == ExecutionStrategy::StaticChunks)
                    family_factor += 0.035;
                if (plan.parallel)
                    family_factor += 0.02 * result.memory_saturation * worker_pressure;
                break;

            case WorkloadFamily::Unknown:
            default:
                break;
            }

            family_factor = std::clamp(family_factor, 0.82, 1.25);
            result.factor = 1.0 +
                (family_factor - 1.0) * result.confidence_weight;
            result.factor = std::clamp(result.factor, 0.88, 1.18);
            result.applied = std::abs(result.factor - 1.0) > 1e-9;
            return result;
        }

        FamilyCalibrationResult apply(
            PlanCostEstimate& estimate,
            const WorkloadFamilyClassification& classification,
            const WorkloadAnalysis& analysis,
            const PerformanceModel& model) const
        {
            const FamilyCalibrationResult result = evaluate(
                estimate.plan,
                classification,
                analysis,
                model);

            estimate.workload_family = classification.family;
            estimate.workload_family_confidence = classification.confidence;
            estimate.family_calibration_factor = result.factor;
            estimate.family_calibration_applied = result.applied;

            if (!result.applied)
                return result;

            estimate.predicted_execution_ms *= result.factor;
            estimate.memory_penalty_ms *= result.factor;
            estimate.imbalance_penalty_ms *= result.factor;

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
