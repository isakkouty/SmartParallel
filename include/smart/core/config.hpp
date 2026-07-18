#pragma once

#include <cstddef>
#include <string>

namespace smart
{
    enum class ExecutionEngineType
    {
        Auto,
        ThreadPool,
        StaticThread,
        OneTbb
    };

    struct Config
    {
        bool enable_timing_diagnostics = false;
        bool enable_experience = true;

        // V1 hybrid runtime policy. Only a compatible PROMOTED artifact may
        // override the analytical decision; every failure falls back safely.
        bool enable_utility_model_runtime = false;
        std::string utility_model_file_path = "smartparallel_utility_model.spm";
        double minimum_utility_model_confidence = 0.60;

        // Optional persistence for the in-process experience database.
        // Persistence is deliberately opt-in: applications decide whether
        // SmartParallel may read or write a file.
        bool enable_experience_persistence = false;
        bool enable_experience_autosave = true;
        std::string experience_file_path = "smartparallel_experience.db";
        std::size_t experience_autosave_interval = 16;

        // Legacy Beta 2.0 runtime predictor. Diagnostic shadow mode is optional.
        // Predictive control is retained only for source compatibility and is
        // ignored by the V1 production decision path.
        bool enable_predictive_shadow = false;
        bool enable_predictive_decisions = false;
        double minimum_predictive_confidence = 0.60;

        // Lazily measures backend launch/scheduling behavior on the current
        // machine and uses those measurements in predictive cost formulas.
        bool enable_machine_runtime_calibration = true;

        // Phase 3 adaptive execution candidate generation. The predictor
        // evaluates multiple worker counts and dynamic chunk sizes while the
        // existing shadow/control safety rules remain unchanged.
        bool enable_adaptive_execution_candidates = true;
        bool enable_chunk_neighborhood_candidates = false;
        std::size_t minimum_adaptive_workers = 2;
        double target_dynamic_chunk_ms = 0.05;
        std::size_t minimum_dynamic_chunk_size = 1;
        std::size_t maximum_dynamic_chunk_size = 65'536;

        // Phase 12.1 stabilization. Synthetic machine probes are treated as
        // bounded priors rather than authoritative speedup truth. StaticThread
        // remains available as an explicit engine, but is excluded from Auto
        // candidate generation until its calibration is demonstrably stable.
        bool enable_static_thread_auto_candidates = false;
        double maximum_machine_calibration_authority = 0.45;
        double minimum_machine_calibration_authority = 0.02;
        double machine_calibration_out_of_domain_scale = 0.20;

        // Historical prediction calibration is safe in shadow mode because it
        // only corrects cost estimates; it does not force predictive control.
        bool enable_prediction_calibration = true;
        std::size_t minimum_calibration_samples = 3;
        double minimum_calibration_confidence = 0.25;

        // Phase 4 experience-aware candidate ranking. The analytical cost
        // model remains the baseline; sufficiently stable execution history
        // can reorder candidates without requiring exact time prediction.
        bool enable_experience_ranking = true;
        std::size_t minimum_ranking_samples = 3;
        double maximum_ranking_history_weight = 0.75;

        // Step 3 model-refinement: historical evidence must earn authority.
        // Sparse, noisy, prediction-inaccurate, or recently contradictory
        // histories are prevented from dominating the analytical baseline.
        bool enable_historical_overconfidence_control = true;
        std::size_t ranking_full_confidence_samples = 24;
        double ranking_stability_scale = 1.50;
        double ranking_prediction_error_scale_percent = 120.0;
        double ranking_recent_regret_soft_limit_percent = 12.0;
        double ranking_recent_disagreement_scale_percent = 25.0;
        double ranking_minimum_negative_evidence_weight = 0.10;

        // Step 4 model-refinement: combine evidence quality into an explicit
        // confidence estimate and add a bounded risk penalty to candidates
        // whose prediction is weak, contradictory, or poorly separated.
        bool enable_confidence_model = true;
        bool enable_confidence_aware_ranking = true;
        double minimum_candidate_model_confidence = 0.15;
        double confidence_disagreement_scale = 0.50;
        double maximum_confidence_risk_penalty = 0.12;
        double full_confidence_score_margin = 0.15;

        // Step 5/6 model refinement: richer workload fingerprints separate
        // access patterns that have similar sizes, while hardware-aware
        // prediction limits unrealistic scaling under cache, SMT and NUMA
        // pressure. Both remain bounded analytical corrections.
        bool enable_enhanced_workload_fingerprint = true;
        bool enable_hardware_aware_prediction = true;
        double hardware_l2_pressure_penalty = 0.08;
        double hardware_numa_penalty = 0.25;

        // Phase 4 learning-policy refinements. Outcomes are learned from
        // relative regret rather than selection alone. Old evidence decays,
        // noisy plans receive lower trust, and nearby workload fingerprints
        // may contribute a bounded transfer signal.
        double ranking_history_decay = 0.97;
        double ranking_success_regret_percent = 3.0;
        double ranking_regret_penalty_scale = 0.35;
        double ranking_uncertainty_penalty_scale = 0.20;
        bool enable_similarity_transfer = true;
        double maximum_similarity_history_weight = 0.20;
        double minimum_similarity = 0.55;

        // Phase 6B bounded family-specific cost correction. The analytical
        // model remains authoritative when classification confidence is low.
        bool enable_family_specific_calibration = false;

        // Decision-quality refinement step 1: model memory-bandwidth
        // saturation explicitly for contiguous cache-exceeding streams.
        bool enable_memory_bandwidth_calibration = true;

        // Phase 8 step 2: independent cache-resident, bandwidth-bound,
        // latency-bound, and large-record memory calibration with bounded
        // confidence authority.
        bool enable_memory_access_calibration = true;

        // Phase 13: derive one canonical memory feature vector and use it as
        // a bounded analytical tie-breaker.
        bool enable_memory_aware_feature_model = true;

        // Phase 14: hybrid analytical cache/latency/bandwidth scaling model.
        bool enable_analytical_runtime_model = true;

        // Phase 10: replace unconditional linear profile extrapolation with
        // a bounded family- and profile-shape-aware scaling model.
        bool enable_learned_runtime_scaling = false;

        // Phase 6C uncertainty-aware residual correction. Exact prediction
        // feedback is preferred; similar workloads may contribute only a
        // tightly bounded signal. The analytical model remains the fallback.
        bool enable_residual_correction = true;
        std::size_t minimum_residual_correction_samples = 3;
        std::size_t residual_full_confidence_samples = 16;
        double minimum_residual_exact_confidence = 0.45;
        double maximum_residual_correction_weight = 0.65;
        double minimum_residual_correction_factor = 0.70;
        double maximum_residual_correction_factor = 1.40;
        bool enable_residual_similarity_transfer = true;
        double maximum_residual_similarity_weight = 0.15;
        double minimum_similarity_residual_factor = 0.85;
        double maximum_similarity_residual_factor = 1.15;

        // Step 1 model-refinement: residual corrections are now shaped by
        // workload family. Memory-sensitive and ambiguous families use more
        // conservative historical influence than stable compute-heavy work.
        bool enable_family_aware_residual_correction = true;
        double compute_residual_weight_scale = 1.00;
        double streaming_residual_weight_scale = 0.65;
        double irregular_residual_weight_scale = 0.75;
        double branch_residual_weight_scale = 0.85;
        double mixed_residual_weight_scale = 0.60;
        double unknown_residual_weight_scale = 0.45;

        // Phase 11 hierarchical residual learning. The analytical predictor is
        // retained as the baseline; online models learn the bounded log ratio
        // log(actual / analytical) using shared, backend, and soft
        // family/backend experts.
        bool enable_hierarchical_residual_learning = true;
        double hierarchical_residual_ridge_lambda = 2.0;
        double hierarchical_residual_decay = 0.995;
        std::size_t hierarchical_shared_minimum_samples = 6;
        std::size_t hierarchical_shared_full_confidence_samples = 24;
        std::size_t hierarchical_backend_minimum_samples = 6;
        std::size_t hierarchical_backend_full_confidence_samples = 20;
        std::size_t hierarchical_family_backend_minimum_samples = 10;
        std::size_t hierarchical_family_backend_full_confidence_samples = 32;
        double hierarchical_residual_variance_scale = 0.20;
        double hierarchical_weak_minimum_factor = 0.90;
        double hierarchical_weak_maximum_factor = 1.10;
        double hierarchical_mature_minimum_factor = 0.70;
        double hierarchical_mature_maximum_factor = 1.40;

        // Exact fingerprint/plan history is a final, high-specificity
        // correction after the hierarchical learner. Similarity transfer is
        // disabled in this mode because generalization belongs to the shared
        // and family/backend experts.
        std::size_t minimum_exact_plan_residual_samples = 8;
        std::size_t exact_plan_residual_full_confidence_samples = 24;
        double minimum_exact_plan_residual_confidence = 0.55;

        // Phase 11 uncertainty-aware selection. Learned evidence may reinforce
        // the analytical winner freely, but overturning it requires stronger
        // confidence and an advantage larger than the uncertainty margin.
        bool enable_risk_aware_ranking = true;
        double runtime_uncertainty_risk_weight = 0.75;
        double machine_calibration_risk_scale = 0.08;
        bool enable_learned_override_guard = true;
        double minimum_learned_override_confidence = 0.60;
        double minimum_learned_override_gain_percent = 3.0;
        double override_uncertainty_margin_scale = 1.0;

        // Phase 12 exact-plan comparison. Once two plans have repeated stable
        // measurements for the same fingerprint, direct empirical evidence may
        // override a badly biased analytical ordering. Both plans must be
        // mature, the measured gain must be material, and confidence intervals
        // must separate (or the gain must exceed a stricter fallback margin).
        bool enable_empirical_plan_override = true;
        std::size_t minimum_empirical_override_samples = 4;
        double minimum_empirical_override_confidence = 0.55;
        double minimum_empirical_override_gain_percent = 5.0;
        double empirical_override_z = 1.645;

        // Historical ranking now uses expected regret plus uncertainty as its
        // primary signal instead of independently counting elapsed ratio,
        // regret, and success derived from the same outcome.
        bool enable_simplified_historical_ranking = true;

        // Phase 5 safe online exploration. Exploration is opt-in and only
        // considers near-best, sufficiently confident alternatives. Harmful
        // experiments trigger a cooldown before another trial is permitted.
        bool enable_online_exploration = false;
        double exploration_probability = 0.05;
        double maximum_exploration_probability = 0.10;
        double maximum_exploration_score_gap_percent = 8.0;
        double minimum_exploration_confidence = 0.55;
        double minimum_exploration_candidate_confidence = 0.35;
        double maximum_exploration_regret_percent = 10.0;
        std::size_t exploration_cooldown_calls = 32;

        ExecutionEngineType execution_engine = ExecutionEngineType::Auto;
    };

    inline Config& global_config()
    {
        static Config config;
        return config;
    }
}
