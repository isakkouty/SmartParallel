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

        // Optional persistence for the in-process experience database.
        // Persistence is deliberately opt-in: applications decide whether
        // SmartParallel may read or write a file.
        bool enable_experience_persistence = false;
        bool enable_experience_autosave = true;
        std::string experience_file_path = "smartparallel_experience.db";
        std::size_t experience_autosave_interval = 16;

        // Phase 2 predictive model. Shadow mode records candidate costs
        // without changing execution. Predictive control remains opt-in
        // until benchmark validation is complete.
        bool enable_predictive_shadow = true;
        bool enable_predictive_decisions = false;
        double minimum_predictive_confidence = 0.60;

        // Lazily measures backend launch/scheduling behavior on the current
        // machine and uses those measurements in predictive cost formulas.
        bool enable_machine_runtime_calibration = true;

        // Phase 3 adaptive execution candidate generation. The predictor
        // evaluates multiple worker counts and dynamic chunk sizes while the
        // existing shadow/control safety rules remain unchanged.
        bool enable_adaptive_execution_candidates = true;
        std::size_t minimum_adaptive_workers = 2;
        double target_dynamic_chunk_ms = 0.05;
        std::size_t minimum_dynamic_chunk_size = 1;
        std::size_t maximum_dynamic_chunk_size = 65'536;

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
        bool enable_family_specific_calibration = true;

        // Decision-quality refinement step 1: model memory-bandwidth
        // saturation explicitly for contiguous cache-exceeding streams.
        bool enable_memory_bandwidth_calibration = true;

        // Phase 8 step 2: independent cache-resident, bandwidth-bound,
        // latency-bound, and large-record memory calibration with bounded
        // confidence authority.
        bool enable_memory_access_calibration = true;

        // Phase 10: replace unconditional linear profile extrapolation with
        // a bounded family- and profile-shape-aware scaling model.
        bool enable_learned_runtime_scaling = true;

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
