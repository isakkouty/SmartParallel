#pragma once

#include <optional>
#include <smart/decision/decision_recommendation.hpp>
#include <smart/decision/decision_report.hpp>
#include <smart/decision/decision_rules.hpp>
#include <smart/decision/execution_hints.hpp>

namespace smart
{
class AnalyticalDecisionProvider
{
  public:
    std::optional<DecisionReport> decide(const DecisionContext& context,
                                         const ExecutionHints& hints) const
    {
        SmallWorkloadRule small_rule;

        if (auto report = small_rule.apply_report(context, hints))
        {
            return report;
        }

        DefaultRule default_rule;

        return default_rule.apply_report(context, hints);
    }

    DecisionRecommendation recommend(const DecisionContext& context,
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
} // namespace smart
