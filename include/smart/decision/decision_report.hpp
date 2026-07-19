#pragma once

#include <smart/decision/decision_source.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/decision/plan_prediction.hpp>
#include <smart/model/execution_characteristics.hpp>
#include <smart/model/performance_model.hpp>
#include <smart/profiling/function_profiler.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <string>
#include <vector>

namespace smart
{
struct DecisionReport
{
    PerformanceModel model;

    WorkloadAnalysis analysis;

    bool has_function_profile = false;
    FunctionProfile function_profile;
    ExecutionPlan plan;

    ExecutionCharacteristics execution;

    double thread_pool_score = 0.0;
    double static_thread_score = 0.0;

    double one_tbb_score = 0.0;

    DecisionSource source = DecisionSource::Analytical;

    double decision_confidence = 1.0;

    // V1 persisted utility-model diagnostics. The analytical plan remains
    // authoritative unless a compatible promoted model passes its gate.
    bool utility_model_loaded = false;
    bool utility_model_promoted = false;
    bool utility_model_compatible = false;
    bool utility_model_applied = false;
    double utility_model_confidence = 0.0;
    std::string utility_model_reason;

    // Phase 2 predictive model. These fields are populated in shadow
    // mode by default and do not alter the selected plan unless the
    // corresponding configuration option is explicitly enabled.
    bool predictive_model_available = false;
    bool predictive_shadow_mode = true;
    bool predictive_plan_applied = false;
    bool predictive_plan_matches_selected = false;
    std::vector<PlanCostEstimate> predictive_candidates;
    ExecutionPlan predictive_plan;

    double predictive_total_ms = 0.0;
    double predictive_confidence = 0.0;

    // Phase 5 online-exploration diagnostics. Exploration remains opt-in
    // and is applied only through the predictive-decision safety gate.
    bool exploration_considered = false;
    bool exploration_applied = false;
    ExecutionPlan exploitation_plan;
    double exploitation_expected_ms = 0.0;
    double exploration_candidate_gap_percent = 0.0;
    std::size_t exploration_eligible_candidates = 0;

    std::size_t exploration_cooldown_remaining = 0;

    // Post-execution prediction feedback. These fields are populated only
    // after the selected plan has actually run.
    bool experience_recorded = false;
    bool experience_persistence_enabled = false;
    bool experience_saved = false;
    double actual_execution_ms = 0.0;
    double selected_plan_predicted_ms = 0.0;
    double prediction_error_percent = 0.0;

    std::size_t experience_samples = 0;
    std::size_t prediction_experience_samples = 0;
    double learned_runtime_correction = 1.0;
};
} // namespace smart
