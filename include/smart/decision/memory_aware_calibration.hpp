#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <smart/decision/plan_prediction.hpp>
#include <smart/model/memory_feature_model.hpp>
#include <smart/model/performance_model.hpp>

namespace smart
{
struct MemoryAwareCalibrationResult
{
    MemoryFeatures features{};
    double factor = 1.0;
    double worker_pressure = 0.0;
    bool applied = false;
};

// Phase 13 bounded memory-aware ranking correction. It does not attempt to
// predict absolute runtime. It only breaks close analytical ties using
// explicit access-pattern, cache-residency and arithmetic-intensity facts.
class MemoryAwareCalibrationPolicy
{
  public:
    MemoryAwareCalibrationResult apply(PlanCostEstimate& estimate,
                                       const MemoryFeatures& features,
                                       const PerformanceModel& model) const
    {
        MemoryAwareCalibrationResult result;
        result.features = features;

        estimate.memory_feature_confidence = features.confidence;
        estimate.memory_feature_access_pattern = features.access_pattern;
        estimate.memory_feature_working_set_tier = features.working_set_tier;
        estimate.memory_feature_bytes_per_iteration = features.bytes_touched_per_iteration;
        estimate.memory_feature_arithmetic_intensity = features.arithmetic_intensity;
        estimate.memory_feature_cache_reuse = features.cache_reuse_score;

        if (!estimate.available || features.confidence < 0.20)
            return result;

        const std::size_t logical = std::max<std::size_t>(1, model.hardware.logical_threads);
        const std::size_t physical = std::max<std::size_t>(
            1, model.hardware.physical_cores == 0 ? logical : model.hardware.physical_cores);
        const std::size_t workers =
            estimate.plan.parallel ? std::max<std::size_t>(1, estimate.plan.job_count) : 1;
        result.worker_pressure = static_cast<double>(workers) / static_cast<double>(physical);

        double raw_factor = 1.0;
        if (features.bandwidth_sensitive && estimate.plan.parallel)
        {
            // Additional workers after physical-core saturation are
            // unlikely to improve a low-intensity stream.
            raw_factor += 0.055 * std::max(0.0, result.worker_pressure - 0.65);
            if (estimate.plan.engine == ExecutionEngineType::StaticThread)
                raw_factor += 0.015;
        }

        if (features.latency_sensitive && estimate.plan.parallel)
        {
            // Latency-bound work benefits from enough concurrency, but
            // static partitioning and severe oversubscription are brittle.
            if (workers < std::min<std::size_t>(4, physical))
                raw_factor += 0.025;
            if (estimate.plan.strategy == ExecutionStrategy::StaticChunks)
                raw_factor += 0.035;
            if (result.worker_pressure > 1.0)
                raw_factor += 0.045 * (result.worker_pressure - 1.0);
            if (estimate.plan.strategy == ExecutionStrategy::DynamicChunks)
                raw_factor -= 0.015;
        }

        if (features.cache_resident && features.cache_reuse_score >= 0.65 && estimate.plan.parallel)
        {
            if (estimate.plan.strategy == ExecutionStrategy::StaticChunks)
                raw_factor -= 0.015;
            if (result.worker_pressure > 1.0)
                raw_factor += 0.02 * (result.worker_pressure - 1.0);
        }

        if (features.large_record && estimate.plan.parallel)
        {
            raw_factor += 0.02 * std::min(1.5, result.worker_pressure);
        }

        raw_factor = std::clamp(raw_factor, 0.96, 1.12);
        result.factor = 1.0 + (raw_factor - 1.0) * features.confidence;
        result.factor = std::clamp(result.factor, 0.97, 1.10);
        result.applied = std::abs(result.factor - 1.0) > 1.0e-9;

        estimate.memory_feature_worker_pressure = result.worker_pressure;
        estimate.memory_aware_calibration_factor = result.factor;
        estimate.memory_aware_calibration_applied = result.applied;
        if (!result.applied)
            return result;

        estimate.predicted_execution_ms *= result.factor;
        estimate.predicted_total_ms =
            estimate.predicted_execution_ms + estimate.imbalance_penalty_ms
            + estimate.scheduling_overhead_ms + estimate.framework_overhead_ms;
        estimate.predicted_speedup =
            estimate.predicted_execution_ms > 0.0
                ? std::max(1.0, estimate.useful_work_ms / estimate.predicted_execution_ms)
                : estimate.predicted_speedup;
        return result;
    }
};
} // namespace smart
