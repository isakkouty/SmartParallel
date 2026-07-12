#pragma once

#include <optional>

#include <smart/decision/decision_provider.hpp>
#include <smart/decision/decision_recommendation.hpp>
#include <smart/experience/historical_decision_provider.hpp>

namespace smart
{
    class CompositeDecisionProvider
    {
    public:
        std::optional<DecisionReport> decide(
            const DecisionContext& context,
            const ExecutionHints& hints) const
        {
            HistoricalDecisionProvider historical;
            AnalyticalDecisionProvider analytical;

            DecisionRecommendation historical_recommendation =
                historical.recommend(context, hints);

            DecisionRecommendation analytical_recommendation =
                analytical.recommend(context, hints);

            if (historical_recommendation.available &&
                analytical_recommendation.available)
            {
                if (historical_recommendation.report.decision_confidence >=
                    analytical_recommendation.report.decision_confidence)
                {
                    return historical_recommendation.report;
                }

                return analytical_recommendation.report;
            }

            if (historical_recommendation.available)
            {
                return historical_recommendation.report;
            }

            if (analytical_recommendation.available)
            {
                return analytical_recommendation.report;
            }

            return std::nullopt;
        }
    };
}
