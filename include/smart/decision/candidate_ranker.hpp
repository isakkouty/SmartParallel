#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <smart/core/config.hpp>
#include <smart/decision/plan_prediction.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/workload/fingerprint.hpp>

namespace smart
{
    class CandidateRanker
    {
    public:
        void rank(
            std::vector<PlanCostEstimate>& candidates,
            const WorkloadFingerprint& fingerprint) const
        {
            if (candidates.empty())
                return;

            double analytical_best = std::numeric_limits<double>::max();
            for (const PlanCostEstimate& candidate : candidates)
            {
                if (candidate.available &&
                    std::isfinite(candidate.predicted_total_ms) &&
                    candidate.predicted_total_ms > 0.0)
                {
                    analytical_best = std::min(
                        analytical_best,
                        candidate.predicted_total_ms);
                }
            }

            if (!std::isfinite(analytical_best) || analytical_best <= 0.0)
                return;

            double exact_best_elapsed = std::numeric_limits<double>::max();
            const ExperienceRecord* exact_record =
                global_experience_database().find_record(fingerprint);
            if (exact_record != nullptr)
            {
                for (const auto& pair : exact_record->plans)
                {
                    const ExperienceEntry& entry = pair.second;
                    const double elapsed = entry.decayed_elapsed_ms > 0.0
                        ? entry.decayed_elapsed_ms
                        : entry.average_elapsed_ms;
                    if (entry.valid && elapsed > 0.0)
                        exact_best_elapsed = std::min(exact_best_elapsed, elapsed);
                }
            }

            for (PlanCostEstimate& candidate : candidates)
            {
                if (!candidate.available)
                    continue;

                candidate.analytical_rank_score =
                    candidate.predicted_total_ms / analytical_best;
                candidate.ranking_score = candidate.analytical_rank_score;

                if (!global_config().enable_experience_ranking)
                    continue;

                const ExperienceEntry* entry =
                    global_experience_database().find_plan(
                        fingerprint,
                        candidate.plan);

                bool exact_used = false;
                double exact_score = 1.0;
                double exact_weight = 0.0;

                if (entry != nullptr &&
                    entry->sample_count >=
                        global_config().minimum_ranking_samples &&
                    entry->average_elapsed_ms > 0.0)
                {
                    const double elapsed = entry->decayed_elapsed_ms > 0.0
                        ? entry->decayed_elapsed_ms
                        : entry->average_elapsed_ms;
                    const double relative_elapsed =
                        std::isfinite(exact_best_elapsed) &&
                        exact_best_elapsed > 0.0
                            ? std::max(1.0, elapsed / exact_best_elapsed)
                            : 1.0;
                    const double regret_penalty =
                        global_config().ranking_regret_penalty_scale *
                        std::max(0.0, entry->decayed_regret_percent) / 100.0;
                    const double success_penalty =
                        (1.0 - std::clamp(
                            entry->decayed_success_rate, 0.0, 1.0)) * 0.25;
                    const double uncertainty = entry->average_elapsed_ms > 0.0
                        ? std::clamp(
                            entry->standard_deviation_ms /
                                entry->average_elapsed_ms,
                            0.0,
                            2.0)
                        : 1.0;
                    const double uncertainty_penalty =
                        global_config().ranking_uncertainty_penalty_scale *
                        uncertainty;

                    exact_score = relative_elapsed + regret_penalty +
                        success_penalty + uncertainty_penalty;

                    const double effective_evidence =
                        entry->effective_sample_weight > 0.0
                            ? entry->effective_sample_weight
                            : static_cast<double>(entry->sample_count);
                    const double full_confidence_samples =
                        static_cast<double>(std::max<std::size_t>(
                            1,
                            global_config().ranking_full_confidence_samples));
                    const double evidence_confidence = std::clamp(
                        1.0 - std::exp(
                            -effective_evidence / full_confidence_samples * 3.0),
                        0.0,
                        1.0);

                    const double coefficient_of_variation =
                        entry->average_elapsed_ms > 0.0
                            ? std::max(
                                0.0,
                                entry->standard_deviation_ms /
                                    entry->average_elapsed_ms)
                            : 1.0;
                    const double stability_confidence = std::clamp(
                        1.0 / (1.0 +
                            global_config().ranking_stability_scale *
                                coefficient_of_variation),
                        0.0,
                        1.0);

                    const double prediction_error_scale = std::max(
                        1.0,
                        global_config().ranking_prediction_error_scale_percent);
                    const double prediction_reliability =
                        entry->prediction_sample_count == 0
                            ? 0.65
                            : std::clamp(
                                1.0 -
                                    entry->average_absolute_prediction_error_percent /
                                        prediction_error_scale,
                                0.10,
                                1.0);

                    const double recent_disagreement = std::abs(
                        entry->last_regret_percent -
                        entry->decayed_regret_percent);
                    const double recent_consistency = std::clamp(
                        std::exp(
                            -recent_disagreement / std::max(
                                1.0,
                                global_config().
                                    ranking_recent_disagreement_scale_percent)),
                        0.0,
                        1.0);

                    const double recent_regret_excess = std::max(
                        0.0,
                        entry->last_regret_percent -
                            global_config().
                                ranking_recent_regret_soft_limit_percent);
                    const double recent_regret_guard = std::clamp(
                        std::exp(
                            -recent_regret_excess / std::max(
                                1.0,
                                global_config().
                                    ranking_recent_regret_soft_limit_percent)),
                        0.0,
                        1.0);

                    double authority = evidence_confidence *
                        stability_confidence * prediction_reliability *
                        recent_consistency * recent_regret_guard;

                    if (!global_config().
                            enable_historical_overconfidence_control)
                    {
                        authority = evidence_confidence *
                            std::max(0.50, stability_confidence);
                    }

                    exact_weight = std::clamp(
                        global_config().maximum_ranking_history_weight *
                            authority,
                        0.0,
                        global_config().maximum_ranking_history_weight);

                    // A consistently poor plan remains useful negative
                    // evidence. Keep a small bounded warning weight, but never
                    // allow it to dominate the analytical model.
                    if (entry->decayed_success_rate < 0.25 &&
                        entry->decayed_regret_percent >
                            global_config().
                                ranking_success_regret_percent)
                    {
                        exact_weight = std::max(
                            exact_weight,
                            std::min(
                                global_config().
                                    ranking_minimum_negative_evidence_weight,
                                global_config().
                                    maximum_ranking_history_weight));
                    }

                    candidate.historical_overconfidence_control_applied =
                        global_config().
                            enable_historical_overconfidence_control;
                    candidate.historical_evidence_confidence =
                        evidence_confidence;
                    candidate.historical_stability_confidence =
                        stability_confidence;
                    candidate.historical_prediction_reliability =
                        prediction_reliability;
                    candidate.historical_recent_consistency =
                        recent_consistency * recent_regret_guard;
                    candidate.historical_effective_weight = exact_weight;

                    candidate.experience_rank_used = exact_weight > 0.0;
                    candidate.historical_rank_score = exact_score;
                    candidate.ranking_history_weight = exact_weight;
                    candidate.ranking_samples = entry->sample_count;
                    candidate.ranking_regret_percent =
                        entry->decayed_regret_percent;
                    candidate.ranking_success_rate =
                        entry->decayed_success_rate;
                    candidate.ranking_uncertainty = uncertainty;
                    exact_used = exact_weight > 0.0;
                }

                double transfer_weight = 0.0;
                double transfer_score = 1.0;
                if (global_config().enable_similarity_transfer)
                {
                    const SimilarExperienceSummary similar =
                        global_experience_database().similar_plan_summary(
                            fingerprint,
                            candidate.plan);
                    if (similar.available)
                    {
                        transfer_score = 1.0 +
                            global_config().ranking_regret_penalty_scale *
                                similar.regret_percent / 100.0 +
                            (1.0 - similar.success_rate) * 0.20;
                        transfer_weight = std::clamp(
                            global_config().maximum_similarity_history_weight *
                                similar.similarity *
                                similar.confidence,
                            0.0,
                            global_config().maximum_similarity_history_weight);
                        candidate.ranking_similarity = similar.similarity;
                        candidate.similarity_rank_used = transfer_weight > 0.0;
                    }
                }

                // Exact evidence takes priority. Similarity is deliberately
                // bounded and only occupies weight not already claimed by
                // exact history.
                transfer_weight = std::min(
                    transfer_weight,
                    std::max(0.0, 1.0 - exact_weight));
                const double analytical_weight = std::max(
                    0.0,
                    1.0 - exact_weight - transfer_weight);
                candidate.ranking_score =
                    analytical_weight * candidate.analytical_rank_score +
                    exact_weight * exact_score +
                    transfer_weight * transfer_score;

                if (exact_used || transfer_weight > 0.0)
                {
                    candidate.confidence = std::clamp(
                        candidate.confidence +
                            0.20 * exact_weight +
                            0.10 * transfer_weight,
                        0.0,
                        1.0);
                }
            }
        }
    };
}
