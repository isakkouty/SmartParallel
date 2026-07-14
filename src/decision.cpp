#include <smart/decision/decision.hpp>
#include <smart/hardware/hardware.hpp>
#include <smart/workload/fingerprint.hpp>
#include <smart/decision/decision_rules.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/decision/decision_report.hpp>
#include <smart/decision/composite_decision_provider.hpp>
#include <smart/decision/predictive_decision_model.hpp>
#include <smart/experience/runtime_experience.hpp>
#include <smart/decision/exploration_policy.hpp>

namespace smart
{
    namespace
    {
        bool same_plan(const ExecutionPlan& left, const ExecutionPlan& right)
        {
            return left.engine == right.engine &&
                left.strategy == right.strategy &&
                left.parallel == right.parallel &&
                left.job_count == right.job_count &&
                left.chunk_size == right.chunk_size;
        }

        void attach_predictive_result(
            DecisionReport& report,
            const Workload& workload,
            const WorkloadAnalysis& analysis,
            const FunctionProfile* function_profile)
        {
            if (!global_config().enable_predictive_shadow &&
                !global_config().enable_predictive_decisions)
            {
                return;
            }

            ensure_experience_loaded();

            PredictiveDecisionModel predictive_model;
            const PredictiveDecisionResult prediction =
                predictive_model.predict(
                    workload,
                    analysis,
                    function_profile);

            report.predictive_shadow_mode =
                !global_config().enable_predictive_decisions;
            report.predictive_model_available = prediction.available;
            report.predictive_candidates = prediction.candidates;

            if (!prediction.available)
            {
                return;
            }

            report.predictive_plan = prediction.recommended_plan;
            report.predictive_total_ms = prediction.recommended_total_ms;
            report.predictive_confidence = prediction.confidence;
            report.predictive_plan_matches_selected =
                same_plan(report.plan, prediction.recommended_plan);
            report.exploitation_plan = prediction.recommended_plan;
            report.exploitation_expected_ms = prediction.recommended_total_ms;

            ExecutionPlan selected_predictive_plan = prediction.recommended_plan;
            if (global_config().enable_online_exploration)
            {
                report.exploration_considered = true;
                const ExplorationDecision exploration =
                    global_online_exploration_policy().select(
                        fingerprint(workload, function_profile),
                        prediction.candidates,
                        prediction.recommended_plan,
                        prediction.confidence);
                report.exploration_applied = exploration.explored;
                report.exploration_candidate_gap_percent =
                    exploration.candidate_gap_percent;
                report.exploration_eligible_candidates =
                    exploration.eligible_candidates;
                report.exploration_cooldown_remaining =
                    exploration.cooldown_remaining;
                if (exploration.explored)
                    selected_predictive_plan = exploration.selected_plan;
            }

            if (global_config().enable_predictive_decisions &&
                prediction.confidence >=
                    global_config().minimum_predictive_confidence)
            {
                report.plan = selected_predictive_plan;
                report.source = DecisionSource::Predictive;
                report.decision_confidence = prediction.confidence;
                report.predictive_plan_applied = true;
                report.predictive_plan_matches_selected =
                    same_plan(report.plan, prediction.recommended_plan);
            }
        }
    }

    ExecutionPlan DecisionEngine::decide(
        const Workload& workload,
        const WorkloadAnalysis& analysis,
        const FunctionProfile* function_profile)
    {
        ExecutionHints hints;
        return decide(workload, analysis, hints, function_profile);
    }

    ExecutionPlan DecisionEngine::decide(
        const Workload& workload,
        const WorkloadAnalysis& analysis,
        const ExecutionHints& hints,
        const FunctionProfile* function_profile)
    {
        DecisionContext context{ workload, analysis, function_profile };

        CompositeDecisionProvider provider;

        auto report = provider.decide(context, hints);

        if (!report)
        {
            last_report_ = DecisionReport{};
            last_report_.analysis = analysis;
            if (function_profile != nullptr)
            {
                last_report_.has_function_profile = true;
                last_report_.function_profile = *function_profile;
            }
            last_report_.plan.parallel = false;
            last_report_.plan.strategy = ExecutionStrategy::Sequential;
            last_report_.plan.job_count = 1;

            attach_predictive_result(
                last_report_, workload, analysis, function_profile);

            return last_report_.plan;
        }

        last_report_ = *report;
        if (function_profile != nullptr)
        {
            last_report_.has_function_profile = true;
            last_report_.function_profile = *function_profile;
        }

        attach_predictive_result(
            last_report_, workload, analysis, function_profile);

        return last_report_.plan;
    }
}
