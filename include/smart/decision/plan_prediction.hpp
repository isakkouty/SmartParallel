#pragma once

#include <cstddef>
#include <smart/decision/execution_plan.hpp>
#include <smart/decision/residual_features.hpp>
#include <smart/model/memory_feature_model.hpp>
#include <smart/workload/observation.hpp>
#include <smart/workload/workload_family.hpp>
#include <vector>

namespace smart
{

enum class MemoryAccessRegime
{
    Unknown,
    CacheResident,
    BandwidthBound,
    LatencyBound,
    LargeRecord
};
struct PlanCostEstimate
{
    ExecutionPlan plan;

    bool available = false;

    double useful_work_ms = 0.0;

    double predicted_execution_ms = 0.0;
    double scheduling_overhead_ms = 0.0;
    bool machine_calibration_used = false;
    double machine_calibration_relative_uncertainty = 0.0;
    double machine_calibration_authority = 0.0;
    double framework_overhead_ms = 0.0;

    double memory_penalty_ms = 0.0;
    double imbalance_penalty_ms = 0.0;
    double predicted_total_ms = 0.0;

    // Stable analytical baseline captured after deterministic component
    // calibration and before any learned residual or historical ranking.
    double analytical_baseline_total_ms = 0.0;
    ResidualFeatureVector residual_features{};

    // Phase 11 hierarchical log-residual diagnostics. Learned corrections
    // modify the observed total only; analytical component ownership is
    // preserved for diagnosis and ablation.
    bool hierarchical_residual_applied = false;
    double hierarchical_residual_base_ms = 0.0;
    double hierarchical_residual_factor = 1.0;
    double hierarchical_residual_log_value = 0.0;
    double hierarchical_residual_confidence = 0.0;
    double hierarchical_residual_log_stddev = 0.0;

    double predicted_runtime_stddev_ms = 0.0;
    double hierarchical_shared_contribution = 0.0;
    double hierarchical_backend_contribution = 0.0;
    double hierarchical_family_backend_contribution = 0.0;

    double hierarchical_shared_confidence = 0.0;

    double hierarchical_backend_confidence = 0.0;
    double hierarchical_family_backend_confidence = 0.0;
    std::size_t hierarchical_shared_samples = 0;
    std::size_t hierarchical_backend_samples = 0;
    std::size_t hierarchical_family_backend_samples = 0;

    // Risk-aware selection and learned-override diagnostics.
    double risk_adjusted_total_ms = 0.0;
    bool learned_override_candidate = false;
    bool learned_override_allowed = true;
    double learned_override_confidence = 0.0;
    double learned_override_gain_percent = 0.0;
    double learned_override_required_margin_percent = 0.0;

    // Phase 12 direct exact-plan comparison diagnostics.
    bool empirical_override_candidate = false;
    bool empirical_override_applied = false;
    double empirical_override_confidence = 0.0;
    double empirical_override_gain_percent = 0.0;
    double empirical_runtime_ms = 0.0;
    double empirical_runtime_ci_low_ms = 0.0;

    double empirical_runtime_ci_high_ms = 0.0;

    // Phase 10 learned runtime-scaling diagnostics.
    bool runtime_scaling_applied = false;
    double runtime_scaling_linear_work_ms = 0.0;
    double runtime_scaling_factor = 1.0;
    double runtime_scaling_exponent = 1.0;
    double runtime_scaling_confidence = 0.0;
    double runtime_scaling_extrapolation_ratio = 1.0;

    bool calibration_applied = false;
    double uncalibrated_total_ms = 0.0;
    double calibration_factor = 1.0;
    std::size_t calibration_samples = 0;

    // Phase 6B family-specific calibration diagnostics. This bounded
    // correction is applied before experience calibration and ranking.
    WorkloadFamily workload_family = WorkloadFamily::Unknown;
    double workload_family_confidence = 0.0;
    bool family_calibration_applied = false;
    double family_calibration_factor = 1.0;

    // Step 1 bandwidth-saturation diagnostics for streaming workloads.
    bool memory_bandwidth_calibration_applied = false;
    double memory_bandwidth_calibration_factor = 1.0;
    double memory_bandwidth_saturation_strength = 0.0;
    double memory_bandwidth_worker_excess = 0.0;
    double memory_bandwidth_bytes_per_iteration = 0.0;
    std::size_t memory_bandwidth_saturation_workers = 1;

    // Phase 8 step 2 specialized memory-regime diagnostics.
    bool memory_access_calibration_applied = false;
    MemoryAccessRegime memory_access_regime = MemoryAccessRegime::Unknown;
    double memory_access_calibration_confidence = 0.0;
    double memory_access_calibration_factor = 1.0;
    double memory_access_worker_pressure = 0.0;

    // Phase 14 hybrid analytical runtime diagnostics.
    bool analytical_runtime_model_applied = false;
    double analytical_runtime_authority = 0.0;
    double analytical_serial_fraction = 0.0;
    double analytical_bandwidth_fraction = 0.0;
    double analytical_speedup_ceiling = 1.0;
    double analytical_original_speedup = 1.0;

    // Phase 13 canonical memory-feature diagnostics and bounded ranking
    // correction. These fields expose why a memory-aware tie was broken.
    bool memory_aware_calibration_applied = false;
    double memory_aware_calibration_factor = 1.0;
    double memory_feature_confidence = 0.0;
    MemoryAccessPattern memory_feature_access_pattern = MemoryAccessPattern::Unknown;
    WorkingSetTier memory_feature_working_set_tier = WorkingSetTier::Unknown;
    double memory_feature_bytes_per_iteration = 0.0;

    double memory_feature_arithmetic_intensity = 0.0;
    double memory_feature_cache_reuse = 0.0;
    double memory_feature_worker_pressure = 0.0;

    // Phase 6C residual-correction diagnostics. The correction is learned
    // from prediction errors and blended according to evidence quality.
    bool residual_correction_applied = false;
    bool residual_exact_history_used = false;
    bool residual_similarity_used = false;
    double pre_residual_total_ms = 0.0;
    double residual_correction_factor = 1.0;
    double residual_correction_confidence = 0.0;

    double residual_correction_uncertainty = 1.0;
    double residual_history_weight = 0.0;
    double residual_similarity = 0.0;
    std::size_t residual_samples = 0;
    bool residual_family_aware = false;
    double residual_family_weight_scale = 1.0;

    double residual_effective_minimum_factor = 1.0;
    double residual_effective_maximum_factor = 1.0;

    double predicted_parallel_efficiency = 0.0;
    double predicted_speedup = 1.0;

    // Phase 4 ranking diagnostics. Lower is better. Analytical ranking
    // is always available; historical ranking is blended in only when
    // enough stable samples exist for this exact plan.
    double analytical_rank_score = 0.0;
    double historical_rank_score = 0.0;
    double ranking_score = 0.0;
    double ranking_history_weight = 0.0;
    std::size_t ranking_samples = 0;
    double ranking_regret_percent = 0.0;

    double ranking_success_rate = 0.0;
    double ranking_uncertainty = 0.0;

    // Step 3 historical-overconfidence diagnostics.
    bool historical_overconfidence_control_applied = false;
    double historical_evidence_confidence = 0.0;
    double historical_stability_confidence = 0.0;
    double historical_prediction_reliability = 0.0;
    double historical_recent_consistency = 0.0;
    double historical_effective_weight = 0.0;

    // Step 4 explicit confidence-model diagnostics.
    double model_profile_confidence = 0.0;
    double model_family_confidence = 0.0;
    double model_residual_confidence = 0.0;
    double model_history_confidence = 0.0;
    double model_similarity_confidence = 0.0;
    double model_agreement_confidence = 0.0;

    double model_confidence = 0.0;
    double model_uncertainty_penalty = 0.0;
    double decision_margin_confidence = 0.0;

    double ranking_similarity = 0.0;
    double similarity_rank_score = 1.0;

    double similarity_rank_confidence = 0.0;
    bool similarity_rank_used = false;
    bool experience_rank_used = false;

    double confidence = 0.0;
};

struct PredictiveDecisionResult
{
    bool available = false;
    std::vector<PlanCostEstimate> candidates;

    ExecutionPlan recommended_plan;
    double recommended_total_ms = 0.0;
    double confidence = 0.0;
};
} // namespace smart
