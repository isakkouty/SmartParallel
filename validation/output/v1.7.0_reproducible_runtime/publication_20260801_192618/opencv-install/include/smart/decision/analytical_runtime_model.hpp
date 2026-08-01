#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <smart/decision/plan_prediction.hpp>
#include <smart/model/memory_feature_model.hpp>
#include <smart/model/performance_model.hpp>

namespace smart
{
struct AnalyticalRuntimeDiagnostics
{
    bool applied = false;
    double authority = 0.0;
    double serial_fraction = 0.0;
    double bandwidth_fraction = 0.0;
    double speedup_ceiling = 1.0;
    double original_speedup = 1.0;

    double corrected_speedup = 1.0;
};

// Phase 14 hybrid analytical model. The profile supplies single-thread
// useful work; this policy only models how much of that work can scale.
// It deliberately owns cache/latency/bandwidth limits so learned residuals
// only need to learn bounded machine-specific error.
class AnalyticalRuntimeModel
{
  public:
    AnalyticalRuntimeDiagnostics apply(PlanCostEstimate& candidate,
                                       const MemoryFeatures& memory,
                                       const PerformanceModel& performance) const
    {
        AnalyticalRuntimeDiagnostics d;
        if (!candidate.available || !candidate.plan.parallel || candidate.plan.job_count <= 1
            || candidate.useful_work_ms <= 0.0)
            return d;

        const double workers = static_cast<double>(candidate.plan.job_count);
        const bool memory_resident = memory.working_set_tier == WorkingSetTier::MemoryResident
                                     || performance.working_set_exceeds_l3;

        const double latency =
            std::clamp(memory.random_access_ratio * (0.55 + 0.45 * memory.dependency_score)
                           * (memory_resident ? 1.0 : 0.55),
                       0.0,
                       0.92);
        const double bandwidth =
            std::clamp(memory.contiguous_ratio * (memory.bandwidth_sensitive ? 1.0 : 0.55)
                           * (memory_resident ? 1.0 : 0.45),
                       0.0,
                       0.90);

        // Latency-bound pointer chains have a large non-parallel service
        // fraction. Bandwidth streams scale initially, then saturate.
        d.serial_fraction =
            std::clamp(0.015 + 0.72 * latency + 0.10 * memory.dependency_score, 0.01, 0.92);
        d.bandwidth_fraction = bandwidth;

        const double amdahl = 1.0 / (d.serial_fraction + (1.0 - d.serial_fraction) / workers);
        const double bandwidth_cap = 1.0 + (workers - 1.0) * (0.78 - 0.58 * bandwidth);
        const double mlp_bonus =
            1.0 + 0.20 * std::clamp(memory.memory_level_parallelism / 4.0, 0.0, 1.0);
        d.speedup_ceiling = std::max(1.0, std::min(amdahl * mlp_bonus, bandwidth_cap));

        d.original_speedup = std::max(1.0, candidate.predicted_speedup);
        d.authority = std::clamp(
            0.35 + 0.55 * memory.confidence + (memory_resident ? 0.10 : 0.0), 0.35, 0.95);

        const double bounded = std::min(d.original_speedup, d.speedup_ceiling);
        d.corrected_speedup = std::exp((1.0 - d.authority) * std::log(d.original_speedup)
                                       + d.authority * std::log(std::max(1.0, bounded)));

        if (std::abs(d.corrected_speedup - d.original_speedup) < 1.0e-9)
            return d;

        const double old_execution = std::max(1.0e-9, candidate.predicted_execution_ms);
        const double imbalance_ratio = candidate.imbalance_penalty_ms / old_execution;
        candidate.predicted_speedup = d.corrected_speedup;
        candidate.predicted_parallel_efficiency =
            std::clamp((d.corrected_speedup - 1.0) / std::max(1.0, workers - 1.0), 0.0, 1.0);
        candidate.predicted_execution_ms = candidate.useful_work_ms / d.corrected_speedup;
        candidate.imbalance_penalty_ms =
            candidate.predicted_execution_ms * std::clamp(imbalance_ratio, 0.0, 1.0);
        candidate.predicted_total_ms =
            candidate.predicted_execution_ms + candidate.imbalance_penalty_ms
            + candidate.scheduling_overhead_ms + candidate.framework_overhead_ms;

        d.applied = true;
        return d;
    }
};
} // namespace smart
