#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <smart/core/config.hpp>
#include <smart/decision/plan_prediction.hpp>

namespace smart
{
    struct ConfidenceAssessment
    {
        double profile_confidence = 0.0;
        double family_confidence = 0.0;
        double residual_confidence = 0.0;
        double history_confidence = 0.0;
        double similarity_confidence = 0.0;
        double agreement_confidence = 0.0;
        double decision_margin_confidence = 0.0;
        double combined_confidence = 0.0;
        double uncertainty_penalty = 0.0;
    };

    class ConfidenceModel
    {
    public:
        static ConfidenceAssessment assess_candidate(
            const PlanCostEstimate& candidate)
        {
            ConfidenceAssessment result;
            result.profile_confidence = std::clamp(candidate.confidence, 0.0, 1.0);
            result.family_confidence = candidate.workload_family_confidence > 0.0
                ? std::clamp(candidate.workload_family_confidence, 0.0, 1.0)
                : 0.50;
            const double exact_residual_confidence =
                candidate.residual_correction_applied
                    ? std::clamp(
                        candidate.residual_correction_confidence, 0.0, 1.0)
                    : 0.0;
            const double hierarchical_confidence =
                std::clamp(
                    candidate.hierarchical_residual_confidence, 0.0, 1.0);
            result.residual_confidence =
                exact_residual_confidence > 0.0 || hierarchical_confidence > 0.0
                    ? std::max(
                        exact_residual_confidence,
                        hierarchical_confidence)
                    : 0.50;

            if (candidate.experience_rank_used)
            {
                result.history_confidence = std::clamp(
                    candidate.historical_evidence_confidence *
                    candidate.historical_stability_confidence *
                    candidate.historical_prediction_reliability *
                    candidate.historical_recent_consistency,
                    0.0,
                    1.0);
            }
            else
            {
                result.history_confidence = 0.50;
            }

            result.similarity_confidence = candidate.similarity_rank_used
                ? std::clamp(
                    candidate.similarity_rank_confidence, 0.0, 1.0)
                : 0.60;

            const double analytical = std::max(1e-9, candidate.analytical_rank_score);
            const double ranked = std::max(1e-9, candidate.ranking_score);
            const double disagreement = std::abs(ranked - analytical) / analytical;
            result.agreement_confidence = std::clamp(
                std::exp(-disagreement / std::max(
                    0.05,
                    global_config().confidence_disagreement_scale)),
                0.0,
                1.0);

            // Weighted geometric mean: one badly unsupported component should
            // reduce trust, but no single missing optional signal can zero it.
            const double weighted_log =
                0.30 * std::log(std::max(0.05, result.profile_confidence)) +
                0.15 * std::log(std::max(0.05, result.family_confidence)) +
                0.20 * std::log(std::max(0.05, result.residual_confidence)) +
                0.15 * std::log(std::max(0.05, result.history_confidence)) +
                0.05 * std::log(std::max(0.05, result.similarity_confidence)) +
                0.15 * std::log(std::max(0.05, result.agreement_confidence));

            result.combined_confidence = std::clamp(
                std::exp(weighted_log),
                global_config().minimum_candidate_model_confidence,
                1.0);
            result.uncertainty_penalty =
                1.0 - result.combined_confidence;
            return result;
        }

        static void apply(std::vector<PlanCostEstimate>& candidates)
        {
            if (candidates.empty())
                return;

            for (PlanCostEstimate& candidate : candidates)
            {
                const ConfidenceAssessment assessment = assess_candidate(candidate);
                candidate.model_profile_confidence = assessment.profile_confidence;
                candidate.model_family_confidence = assessment.family_confidence;
                candidate.model_residual_confidence = assessment.residual_confidence;
                candidate.model_history_confidence = assessment.history_confidence;
                candidate.model_similarity_confidence = assessment.similarity_confidence;
                candidate.model_agreement_confidence = assessment.agreement_confidence;
                candidate.model_confidence = assessment.combined_confidence;
                candidate.model_uncertainty_penalty = assessment.uncertainty_penalty;
                candidate.confidence = assessment.combined_confidence;

                if (global_config().enable_confidence_aware_ranking &&
                    (!global_config().enable_risk_aware_ranking ||
                     candidate.risk_adjusted_total_ms <= 0.0))
                {
                    // Legacy fallback. Phase 11 normally represents risk in
                    // runtime units before normalization, avoiding a second
                    // independent uncertainty penalty here.
                    candidate.ranking_score *= 1.0 +
                        global_config().maximum_confidence_risk_penalty *
                        assessment.uncertainty_penalty;
                }
            }

            std::vector<double> scores;
            scores.reserve(candidates.size());
            for (const PlanCostEstimate& candidate : candidates)
                scores.push_back(candidate.ranking_score);
            std::sort(scores.begin(), scores.end());

            const double best = scores.front();
            const double second = scores.size() > 1 ? scores[1] : best;
            const double margin = best > 0.0
                ? std::max(0.0, second - best) / best
                : 0.0;
            const double margin_confidence = std::clamp(
                margin / std::max(0.01, global_config().full_confidence_score_margin),
                0.0,
                1.0);

            for (PlanCostEstimate& candidate : candidates)
            {
                candidate.decision_margin_confidence = margin_confidence;
                candidate.confidence = std::clamp(
                    candidate.model_confidence *
                        (0.75 + 0.25 * margin_confidence),
                    global_config().minimum_candidate_model_confidence,
                    1.0);
            }
        }
    };
}
