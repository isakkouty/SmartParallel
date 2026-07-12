#pragma once

#include <optional>

#include <smart/decision/decision_report.hpp>
#include <smart/decision/decision_rules.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/decision/execution_hints.hpp>
#include <smart/decision/decision_provider.hpp>
#include <smart/workload/fingerprint.hpp>
#include <smart/decision/decision_recommendation.hpp>

namespace smart
{
    class HistoricalDecisionProvider
    {
    public:
        std::optional<DecisionReport> decide(
            const DecisionContext& context,
            const ExecutionHints& hints) const
        {
            WorkloadFingerprint fp =
                fingerprint(context.workload);

            const ExperienceEntry* entry =
                global_experience_database().best_entry(fp);

            if (!entry)
                return std::nullopt;

            if (entry->confidence < 0.8)
                return std::nullopt;

            AnalyticalDecisionProvider analytical;
            DecisionReport report =
                analytical.decide(context, hints).value();

            report.plan.engine = entry->engine;
            report.plan.strategy = entry->strategy;
            report.plan.job_count = entry->job_count;
            report.plan.parallel = entry->strategy != ExecutionStrategy::Sequential;
            report.source = DecisionSource::Historical;
            report.decision_confidence = entry->confidence;

            return report;
        }

        DecisionRecommendation recommend(
            const DecisionContext& context,
            const ExecutionHints& hints) const
        {
            DecisionRecommendation recommendation;

            auto report = decide(context, hints);

            if (!report)
                return recommendation;

            recommendation.report = *report;
            recommendation.available = true;

            return recommendation;
        }
    };
}
