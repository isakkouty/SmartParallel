#pragma once

#include <cstddef>
#include <string>

namespace smart
{
namespace runtime_limits
{
// Central production defaults for process-lifetime and per-root retained state.
// A configured value of zero selects these defaults; it never enables unbounded
// retention. Feature enable flags remain the way to disable a cache entirely.
inline constexpr std::size_t profile_cache_entries = 4096;
inline constexpr std::size_t experience_records = 4096;
inline constexpr std::size_t experience_plans_per_record = 64;
inline constexpr std::size_t exploration_states = 4096;
inline constexpr std::size_t nested_trace_records = 65536;
inline constexpr std::size_t nested_plan_snapshots = 4096;
inline constexpr std::size_t backend_calibration_states = 4096;
inline constexpr std::size_t algorithm_dispatch_entries = 2048;

inline constexpr std::size_t bounded_limit(std::size_t configured,
                                           std::size_t production_default) noexcept
{
    return configured == 0 ? production_default : configured;
}
} // namespace runtime_limits

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
    // Process-lifetime adaptive history is bounded independently from the
    // parallel_for profile cache. Zero selects the central production default.
    std::size_t experience_cache_max_records = runtime_limits::experience_records;
    std::size_t experience_cache_max_plans_per_record =
        runtime_limits::experience_plans_per_record;
    std::size_t online_exploration_state_max_entries = runtime_limits::exploration_states;

    // Automatic parallel_for callback sampling. A small prefix is executed
    // exactly once and timed before the remaining range is scheduled. This
    // lets the decision engine estimate callback cost without requiring
    // user hints or duplicating side effects.
    bool enable_parallel_for_auto_profiling = true;
    std::size_t parallel_for_profile_min_samples = 8;
    std::size_t parallel_for_profile_max_samples = 64;
    double parallel_for_profile_min_signal_ms = 0.01;

    double parallel_for_estimated_overhead_ms = 1.0;

    std::size_t parallel_for_profile_regions = 3;

    bool enable_parallel_for_profile_cache = true;
    std::size_t parallel_for_profile_cache_min_hits = 1;
    double parallel_for_profile_cache_blend = 0.25;
    // Bound long-running cache growth. Zero selects the central production
    // default; unbounded retention is intentionally unsupported.
    std::size_t parallel_for_profile_cache_max_entries =
        runtime_limits::profile_cache_entries;
    // Nested structure is workload data, not a permanent callsite property.
    // Decay old evidence so a phase-changing callsite can stop being treated as
    // a nested frontier candidate after repeated non-nested observations.
    double parallel_for_profile_nested_evidence_blend = 0.50;
    double parallel_for_profile_nested_evidence_threshold = 0.50;

    double parallel_for_minimum_predicted_speedup = 1.10;
    double parallel_for_imbalance_penalty = 1.10;
    // Applications that mutate custom decision inputs at runtime can increment
    // this generation to invalidate cached profiles and stable plans atomically.
    std::size_t parallel_for_policy_generation = 0;

    // Nested granularity guard. After backend negotiation, cap the effective
    // concurrency by the amount of schedulable work. A nested range that
    // cannot keep at least two workers useful executes as an explicit
    // sequential fallback instead of paying nested scheduler overhead.
    bool enable_nested_granularity_enforcement = true;
    std::size_t nested_min_iterations_per_worker = 8;
    std::size_t nested_min_chunks_per_worker = 1;
    std::size_t nested_target_chunks_per_worker = 2;

    // Root-scoped nested execution. A session gives every SmartParallel loop in
    // one nested computation a shared concurrency envelope and a stable plan
    // snapshot. Zero selects the machine hardware-thread count.
    bool enable_nested_execution_session = true;
    std::size_t nested_root_concurrency_budget = 0;

    // Prefer one useful parallel frontier in a SmartParallel-only nest. Outer
    // levels that expose fewer iterations than the root worker budget are
    // deferred when profiling has observed deeper SmartParallel calls. Once a
    // level owns the frontier, descendants execute sequentially by default.
    bool enable_nested_parallel_frontier = true;
    bool enable_nested_frontier_deferral = true;
    bool enable_nested_frontier_promotion = true;

    // Once a parallel frontier is established, descendant public parallel_for
    // calls may execute directly under the inherited context. This bypasses
    // cache keys, profiling, plan lookup and backend negotiation while keeping
    // the already-selected sequential descendant semantics. Detailed lineage
    // is retained automatically when tracing or conservative learning is active.
    bool enable_frontier_descendant_direct_mode = true;

    // Reuse a fully resolved plan inside one root session before consulting the
    // process-wide profile cache again. The memo is bounded by the existing
    // per-root snapshot limit and never survives the root session.
    bool enable_session_local_plan_memo = true;

    // Time-based nested profitability and chunking. These values are deliberately
    // conservative and can be tuned from the trace produced by the benchmark.
    double nested_min_parallel_work_ms = 0.10;
    double nested_target_chunk_ms = 0.05;
    double nested_plan_hysteresis = 1.15;

    // Nested calls learn from the work they actually execute instead of
    // recursively pre-running callback samples. This preserves exactly-once
    // semantics and avoids profiling the scheduler from inside itself.
    bool enable_nested_online_telemetry = true;

    // Cold root calls that participate in nested execution are learned from one
    // conservative exactly-once execution instead of pre-running callback samples
    // under a provisional plan. Descendants remain sequential for that learning
    // pass, then the next invocation uses the collected per-depth telemetry.
    bool enable_nested_root_online_telemetry = true;

    // Large cold roots can use a conservative analytical parallel plan while
    // learning from the real exactly-once execution. Underfilled roots remain
    // sequential so deeper frontier discovery is unchanged.
    bool enable_root_analytical_cold_start = true;
    std::size_t root_analytical_cold_min_iterations_per_worker = 4;
    // For a cold root with only one coarse item per worker, execute one item
    // exactly once as an in-band pilot. If that observed item predicts enough
    // total work, schedule only the remaining items in parallel. This improves
    // recursive/coarse roots without speculatively rerunning callbacks.
    bool enable_root_pilot_cold_start = true;
    double root_pilot_cold_min_estimated_work_ms = 1.0;

    // Structured diagnostics are opt-in because recording every loop has a
    // measurable cost on very small workloads.
    bool enable_nested_execution_trace = false;
    // Trace collection is diagnostic and process-wide. Bound retained records
    // so enabling it in a long-running service cannot consume memory forever.
    // Zero selects the central production default; retained traces are never
    // unbounded.
    std::size_t nested_execution_trace_max_records =
        runtime_limits::nested_trace_records;
    // A single unusually long root can encounter many dynamic profile keys.
    // Snapshotting is an optimization, so safely stop adding new entries once
    // this per-root bound is reached. Zero selects the production default.
    std::size_t nested_plan_snapshot_max_entries =
        runtime_limits::nested_plan_snapshots;

    // Cooperative ThreadPool helper recruitment. Only idle workers are asked to
    // help, tiny regions recruit fewer helpers, and queued zero-work helpers are
    // cancelled after useful work completes.
    std::size_t thread_pool_min_chunks_per_helper = 1;
    bool thread_pool_cancel_idle_helpers = true;

    // Optional bounded backend calibration. The feature is disabled for the
    // core by default and enabled by the real-world benchmark suite. It tests
    // ThreadPool versus oneTBB only after a stable profile exists, then keeps
    // the winner behind hysteresis.
    bool enable_parallel_for_backend_calibration = false;
    std::size_t parallel_for_backend_calibration_min_samples = 1;
    double parallel_for_backend_calibration_hysteresis_percent = 8.0;
    std::size_t parallel_for_backend_calibration_max_entries =
        runtime_limits::backend_calibration_states;

    // Optimization: when a reliable cached callback profile already predicts
    // that parallel execution cannot meet the minimum speedup, bypass workload
    // analysis and decision ranking and execute the range directly. This keeps
    // expensive small ranges eligible for profiling while removing repeated
    // scheduler overhead from known-cheap callbacks.
    bool enable_parallel_for_cached_sequential_fast_path = true;
    // A stable sub-millisecond descendant under an already sealed frontier may
    // bypass adaptive planning. At the root, the absolute-cost bypass is limited
    // to a single coarse item with nested work and measured sequential
    // profitability; profitable multi-item roots remain eligible for parallelism.
    bool enable_parallel_for_tiny_work_bypass = true;
    double parallel_for_tiny_work_bypass_max_ms = 1.0;
    std::size_t parallel_for_tiny_work_bypass_min_observations = 3;
    // A sequential bypass is enabled only after this many independently
    // sampled profiles agree. Cache hits alone do not increase confidence.
    std::size_t parallel_for_sequential_fast_path_min_observations = 3;
    // Require a margin below the normal break-even threshold, preventing
    // borderline estimates from becoming sticky sequential decisions.
    double parallel_for_sequential_fast_path_speedup_margin = 0.85;
    // After this many bypasses, force a fresh regional sample so changing
    // callback costs can promote the workload back to a parallel backend.
    std::size_t parallel_for_sequential_fast_path_revalidate_interval = 16;

    // Cached parallel plans also receive periodic end-to-end revalidation so a
    // phase-changing workload cannot keep a stale frontier indefinitely.
    std::size_t parallel_for_stable_plan_revalidate_interval = 64;
    // Use-count revalidation alone is insufficient for low-frequency callsites in
    // long-running services. Force a fresh observation after this wall-clock age.
    // Zero disables age-based revalidation.
    std::size_t parallel_for_profile_revalidate_after_ms = 300'000;

    // v1.4 cheap-algorithm dispatch. Eligible root Auto calls learn from real
    // complete sequential and scheduled invocations, then bypass chunk and
    // scheduler construction when direct sequential execution wins.
    bool enable_parallel_algorithm_hot_dispatch = true;
    std::size_t parallel_algorithm_hot_dispatch_max_entries =
        runtime_limits::algorithm_dispatch_entries;
    double parallel_algorithm_hot_dispatch_minimum_parallel_speedup = 1.12;
    double parallel_algorithm_hot_dispatch_probe_max_ms = 5.0;
    double parallel_algorithm_hot_dispatch_blend = 0.25;
    std::size_t parallel_algorithm_hot_dispatch_revalidate_interval = 64;
    std::size_t parallel_algorithm_hot_dispatch_search_revalidate_interval = 32;

    // Analytical workload thresholds. These preserve the previous defaults
    // but are configurable instead of being embedded in decision rules.
    std::size_t small_workload_iteration_threshold = 1'000;
    std::size_t cheap_workload_sequential_threshold = 100'000;
    std::size_t many_iterations_threshold = 1'000'000;

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
} // namespace smart
