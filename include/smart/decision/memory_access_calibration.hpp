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
    struct MemoryAccessCalibrationResult
    {
        MemoryAccessRegime regime = MemoryAccessRegime::Unknown;
        double confidence = 0.0;
        double factor = 1.0;
        double worker_pressure = 0.0;
        double random_access_ratio = 0.0;
        double contiguous_ratio = 0.0;
        double l3_pressure = 0.0;
        double bytes_per_iteration = 0.0;
        bool applied = false;
    };

    // Phase 8, step 2: split memory-sensitive workloads into independent
    // cache-resident, bandwidth-bound, latency-bound, and large-record
    // regimes. Each regime receives a small bounded correction instead of a
    // single generic memory rule. Classification confidence limits how much
    // authority the correction receives.
    class MemoryAccessCalibrationPolicy
    {
    public:
        MemoryAccessCalibrationResult evaluate(
            const ExecutionPlan& plan,
            const WorkloadFamilyClassification& classification,
            const WorkloadAnalysis& analysis,
            const PerformanceModel& model) const
        {
            MemoryAccessCalibrationResult result;
            if ((classification.family != WorkloadFamily::StreamingMemory &&
                 classification.family != WorkloadFamily::IrregularMemory) ||
                analysis.structural.logical_iterations == 0)
            {
                return result;
            }

            std::size_t contiguous_known = 0;
            std::size_t contiguous = 0;
            std::size_t random_known = 0;
            std::size_t random = 0;
            bool storage_implies_irregular_access = false;
            for (const DimensionAnalysis& dimension : analysis.structural.dimensions)
            {
                if (dimension.contiguous_known)
                {
                    ++contiguous_known;
                    contiguous += dimension.contiguous ? 1u : 0u;
                }
                if (dimension.random_access_known)
                {
                    ++random_known;
                    random += dimension.random_access ? 1u : 0u;
                }
                if (dimension.storage_kind == StorageKind::NodeBased ||
                    dimension.storage_kind == StorageKind::Segmented)
                {
                    storage_implies_irregular_access = true;
                }
            }

            result.contiguous_ratio = contiguous_known == 0
                ? (classification.family == WorkloadFamily::StreamingMemory ? 0.70 : 0.20)
                : static_cast<double>(contiguous) / static_cast<double>(contiguous_known);
            // Unknown semantic randomness must remain unknown. Do not
            // synthesize a random-access ratio from the already-derived
            // workload family, otherwise classification and calibration form
            // a self-reinforcing feedback loop.
            result.random_access_ratio = random_known == 0
                ? 0.0
                : static_cast<double>(random) / static_cast<double>(random_known);
            result.l3_pressure = std::max(
                0.0,
                analysis.structural.cache_ratios_available
                    ? analysis.structural.l3_residency_ratio
                    : model.l3_pressure);
            result.bytes_per_iteration =
                static_cast<double>(analysis.structural.represented_input_bytes) /
                static_cast<double>(analysis.structural.logical_iterations);

            const bool cache_resident = result.l3_pressure > 0.0 &&
                result.l3_pressure <= 0.85 &&
                result.random_access_ratio < 0.45;
            const bool latency_bound =
                (random_known > 0 && result.random_access_ratio >= 0.50) ||
                storage_implies_irregular_access;
            const bool large_record = analysis.objects_are_large ||
                result.bytes_per_iteration >= 128.0;

            if (latency_bound)
                result.regime = MemoryAccessRegime::LatencyBound;
            else if (cache_resident)
                result.regime = MemoryAccessRegime::CacheResident;
            else if (large_record)
                result.regime = MemoryAccessRegime::LargeRecord;
            else
                result.regime = MemoryAccessRegime::BandwidthBound;

            double structural_confidence = 0.25;
            if (contiguous_known > 0)
                structural_confidence += 0.15;
            if (random_known > 0 || storage_implies_irregular_access)
                structural_confidence += 0.25;
            if (analysis.structural.cache_ratios_available)
                structural_confidence += 0.20;
            if (analysis.structural.represented_input_bytes > 0)
                structural_confidence += 0.10;
            result.confidence = std::clamp(
                classification.confidence * structural_confidence,
                0.0,
                1.0);

            const std::size_t logical = std::max<std::size_t>(1, model.hardware.logical_threads);
            const std::size_t physical = std::max<std::size_t>(
                1,
                model.hardware.physical_cores == 0
                    ? logical
                    : model.hardware.physical_cores);
            const std::size_t workers = plan.parallel
                ? std::max<std::size_t>(1, plan.job_count)
                : 1;
            result.worker_pressure = std::clamp(
                static_cast<double>(workers) / static_cast<double>(physical),
                0.0,
                2.0);

            double raw_factor = 1.0;
            switch (result.regime)
            {
            case MemoryAccessRegime::CacheResident:
                // Cache residency is a weak structural prior. Keep the cold
                // correction deliberately small and execution-only.
                if (plan.parallel && workers <= physical)
                    raw_factor -= 0.01;
                else if (!plan.parallel)
                    raw_factor += 0.01;
                break;

            case MemoryAccessRegime::BandwidthBound:
                // Phase 8 step 1 owns bandwidth saturation. Applying another
                // bandwidth factor here would double-count the same signal.
                return result;

            case MemoryAccessRegime::LatencyBound:
                // Only explicit/high-confidence semantic randomness or a
                // node/segmented storage observation may enter this regime.
                if (plan.parallel && workers > physical)
                    raw_factor += 0.04 * (result.worker_pressure - 1.0);
                if (plan.parallel &&
                    plan.strategy == ExecutionStrategy::StaticChunks)
                    raw_factor += 0.02;
                break;

            case MemoryAccessRegime::LargeRecord:
                if (plan.parallel)
                    raw_factor += 0.03 * result.worker_pressure;
                break;

            case MemoryAccessRegime::Unknown:
            default:
                return result;
            }

            raw_factor = std::clamp(raw_factor, 0.97, 1.05);
            result.factor = 1.0 + (raw_factor - 1.0) * result.confidence;
            result.factor = std::clamp(result.factor, 0.97, 1.05);
            result.applied = result.confidence >= 0.15 &&
                std::abs(result.factor - 1.0) > 1.0e-9;
            return result;
        }

        MemoryAccessCalibrationResult apply(
            PlanCostEstimate& estimate,
            const WorkloadFamilyClassification& classification,
            const WorkloadAnalysis& analysis,
            const PerformanceModel& model) const
        {
            const auto result = evaluate(
                estimate.plan, classification, analysis, model);

            estimate.memory_access_regime = result.regime;
            estimate.memory_access_calibration_confidence = result.confidence;
            estimate.memory_access_calibration_factor = result.factor;
            estimate.memory_access_worker_pressure = result.worker_pressure;
            estimate.memory_access_calibration_applied = result.applied;

            if (!result.applied)
                return result;

            estimate.predicted_execution_ms *= result.factor;
            estimate.predicted_total_ms =
                estimate.predicted_execution_ms +
                estimate.imbalance_penalty_ms +
                estimate.scheduling_overhead_ms +
                estimate.framework_overhead_ms;
            estimate.predicted_speedup = estimate.predicted_execution_ms > 0.0
                ? std::max(1.0, estimate.useful_work_ms /
                    estimate.predicted_execution_ms)
                : estimate.predicted_speedup;

            // The correction magnitude is already confidence-blended. Do not
            // penalize candidate confidence a second time downstream.
            return result;
        }
    };
}
