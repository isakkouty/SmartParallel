#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <smart/core/config.hpp>
#include <smart/decision/plan_prediction.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/workload/fingerprint.hpp>
#include <vector>

namespace smart
{
class CandidateRanker
{
  public:
    void rank(std::vector<PlanCostEstimate>& candidates,
              const WorkloadFingerprint& fingerprint) const
    {
        if (candidates.empty())
            return;

        const Config& config = global_config();
        double analytical_best = std::numeric_limits<double>::max();
        std::size_t baseline_index = candidates.size();
        double baseline_total = std::numeric_limits<double>::max();
        bool explicit_stable_baseline = false;

        for (std::size_t index = 0; index < candidates.size(); ++index)
        {
            PlanCostEstimate& candidate = candidates[index];
            if (!candidate.available || !positive(candidate.predicted_total_ms))
            {
                continue;
            }

            const double learned_sigma = std::max(0.0, candidate.predicted_runtime_stddev_ms);
            const double base_sigma = candidate.predicted_total_ms * 0.05
                                      * (1.0 - std::clamp(candidate.confidence, 0.0, 1.0));
            const double calibration_sigma =
                candidate.machine_calibration_used
                    ? candidate.predicted_total_ms * config.machine_calibration_risk_scale
                          * std::clamp(candidate.machine_calibration_relative_uncertainty, 0.0, 1.0)
                    : 0.0;
            const double sigma = learned_sigma + base_sigma + calibration_sigma;
            candidate.risk_adjusted_total_ms =
                config.enable_risk_aware_ranking
                    ? candidate.predicted_total_ms + config.runtime_uncertainty_risk_weight * sigma
                    : candidate.predicted_total_ms;
            analytical_best = std::min(analytical_best, candidate.risk_adjusted_total_ms);

            const bool has_explicit_baseline = positive(candidate.analytical_baseline_total_ms);
            explicit_stable_baseline = explicit_stable_baseline || has_explicit_baseline;
            const double stable_baseline = has_explicit_baseline
                                               ? candidate.analytical_baseline_total_ms
                                               : candidate.predicted_total_ms;
            if (stable_baseline < baseline_total)
            {
                baseline_total = stable_baseline;
                baseline_index = index;
            }
        }

        if (!positive(analytical_best))
            return;

        for (PlanCostEstimate& candidate : candidates)
        {
            if (!candidate.available)
                continue;

            candidate.analytical_rank_score = candidate.risk_adjusted_total_ms / analytical_best;
            candidate.ranking_score = candidate.analytical_rank_score;
            candidate.historical_rank_score = 1.0;

            if (!config.enable_experience_ranking)
                continue;

            apply_exact_history(candidate, fingerprint, config);
            apply_similarity_history(candidate, fingerprint, config);

            const double exact_weight = candidate.ranking_history_weight;
            const double transfer_weight =
                std::min(candidate.similarity_rank_used ? config.maximum_similarity_history_weight
                                                              * candidate.ranking_similarity
                                                              * candidate.similarity_rank_confidence
                                                        : 0.0,
                         std::max(0.0, 1.0 - exact_weight));
            const double analytical_weight = std::max(0.0, 1.0 - exact_weight - transfer_weight);
            const double transfer_score =
                candidate.similarity_rank_used ? candidate.similarity_rank_score : 1.0;

            candidate.ranking_score = analytical_weight * candidate.analytical_rank_score
                                      + exact_weight * candidate.historical_rank_score
                                      + transfer_weight * transfer_score;

            if (candidate.experience_rank_used || candidate.similarity_rank_used)
            {
                candidate.confidence = std::clamp(
                    candidate.confidence + 0.15 * exact_weight + 0.05 * transfer_weight, 0.0, 1.0);
            }
        }

        if (config.enable_learned_override_guard && explicit_stable_baseline
            && baseline_index < candidates.size())
        {
            enforce_override_guard(candidates, baseline_index, config);
        }

        if (config.enable_empirical_plan_override)
            apply_empirical_plan_override(candidates, fingerprint, config);
    }

  private:
    static void apply_exact_history(PlanCostEstimate& candidate,
                                    const WorkloadFingerprint& fingerprint,
                                    const Config& config)
    {
        const ExperienceEntry* entry =
            global_experience_database().find_plan(fingerprint, candidate.plan);
        if (entry == nullptr || entry->sample_count < config.minimum_ranking_samples
            || entry->average_elapsed_ms <= 0.0)
        {
            return;
        }

        const double uncertainty =
            entry->average_elapsed_ms > 0.0
                ? std::clamp(entry->standard_deviation_ms / entry->average_elapsed_ms, 0.0, 2.0)
                : 1.0;
        const double regret = std::max(0.0, entry->decayed_regret_percent);

        // In simplified mode historical regret is already the empirical
        // runtime ratio relative to the best plan observed in the same
        // round. Preserve that scale instead of shrinking it by another
        // arbitrary coefficient; otherwise even repeated 15-30% losses
        // cannot overturn a biased analytical ordering.
        double exact_score =
            config.enable_simplified_historical_ranking
                ? 1.0 + regret / 100.0 + 0.10 * uncertainty
                : 1.0 + config.ranking_regret_penalty_scale * regret / 100.0
                      + config.ranking_uncertainty_penalty_scale * uncertainty
                      + (1.0 - std::clamp(entry->decayed_success_rate, 0.0, 1.0)) * 0.20;

        const double effective_evidence = entry->effective_sample_weight > 0.0
                                              ? entry->effective_sample_weight
                                              : static_cast<double>(entry->sample_count);
        const double full_confidence_samples =
            static_cast<double>(std::max<std::size_t>(1, config.ranking_full_confidence_samples));
        const double evidence_confidence = std::clamp(
            1.0 - std::exp(-effective_evidence / full_confidence_samples * 3.0), 0.0, 1.0);

        const double coefficient_of_variation =
            entry->average_elapsed_ms > 0.0
                ? std::max(0.0, entry->standard_deviation_ms / entry->average_elapsed_ms)
                : 1.0;

        const double stability_confidence = std::clamp(
            1.0 / (1.0 + config.ranking_stability_scale * coefficient_of_variation), 0.0, 1.0);
        const double prediction_error_scale =
            std::max(1.0, config.ranking_prediction_error_scale_percent);
        const double prediction_reliability =
            entry->prediction_sample_count == 0
                ? 0.65
                : std::clamp(1.0
                                 - entry->average_absolute_prediction_error_percent
                                       / prediction_error_scale,
                             0.10,
                             1.0);
        const double recent_disagreement =
            std::abs(entry->last_regret_percent - entry->decayed_regret_percent);
        const double recent_consistency =
            std::clamp(std::exp(-recent_disagreement
                                / std::max(1.0, config.ranking_recent_disagreement_scale_percent)),
                       0.0,
                       1.0);
        const double recent_regret_excess = std::max(
            0.0, entry->last_regret_percent - config.ranking_recent_regret_soft_limit_percent);

        const double recent_regret_guard =
            std::clamp(std::exp(-recent_regret_excess
                                / std::max(1.0, config.ranking_recent_regret_soft_limit_percent)),
                       0.0,
                       1.0);

        double authority = evidence_confidence * stability_confidence * prediction_reliability
                           * recent_consistency * recent_regret_guard;
        if (!config.enable_historical_overconfidence_control)
        {
            authority = evidence_confidence * std::max(0.50, stability_confidence);
        }

        double exact_weight = std::clamp(config.maximum_ranking_history_weight * authority,
                                         0.0,
                                         config.maximum_ranking_history_weight);
        if (entry->decayed_success_rate < 0.25
            && entry->decayed_regret_percent > config.ranking_success_regret_percent)
        {
            exact_weight = std::max(exact_weight,
                                    std::min(config.ranking_minimum_negative_evidence_weight,
                                             config.maximum_ranking_history_weight));
        }

        candidate.historical_overconfidence_control_applied =
            config.enable_historical_overconfidence_control;
        candidate.historical_evidence_confidence = evidence_confidence;
        candidate.historical_stability_confidence = stability_confidence;
        candidate.historical_prediction_reliability = prediction_reliability;
        candidate.historical_recent_consistency = recent_consistency * recent_regret_guard;
        candidate.historical_effective_weight = exact_weight;
        candidate.experience_rank_used = exact_weight > 0.0;
        candidate.historical_rank_score = exact_score;
        candidate.ranking_history_weight = exact_weight;
        candidate.ranking_samples = entry->sample_count;
        candidate.ranking_regret_percent = entry->decayed_regret_percent;
        candidate.ranking_success_rate = entry->decayed_success_rate;
        candidate.ranking_uncertainty = uncertainty;
    }

    static void apply_similarity_history(PlanCostEstimate& candidate,
                                         const WorkloadFingerprint& fingerprint,
                                         const Config& config)
    {
        candidate.similarity_rank_score = 1.0;
        candidate.similarity_rank_confidence = 0.0;
        if (!config.enable_similarity_transfer)
            return;

        const SimilarExperienceSummary similar =
            global_experience_database().similar_plan_summary(fingerprint, candidate.plan);
        if (!similar.available)
            return;

        candidate.ranking_similarity = similar.similarity;
        candidate.similarity_rank_used =
            similar.similarity >= config.minimum_similarity && similar.confidence > 0.0;
        if (!candidate.similarity_rank_used)
            return;

        const double uncertainty = 1.0 - std::clamp(similar.confidence, 0.0, 1.0);
        candidate.similarity_rank_score =
            1.0 + config.ranking_regret_penalty_scale * similar.regret_percent / 100.0
            + config.ranking_uncertainty_penalty_scale * uncertainty;
        candidate.similarity_rank_confidence = std::clamp(similar.confidence, 0.0, 1.0);
    }

    struct EmpiricalEvidence
    {
        bool available = false;
        double mean_ms = 0.0;
        double confidence = 0.0;
        double ci_low_ms = 0.0;
        double ci_high_ms = 0.0;
    };

    static EmpiricalEvidence empirical_evidence(const ExperienceEntry* entry, const Config& config)
    {
        EmpiricalEvidence result;
        if (entry == nullptr || entry->sample_count < config.minimum_empirical_override_samples)
        {
            return result;
        }

        result.mean_ms = entry->outcome_sample_count > 0 && entry->decayed_elapsed_ms > 0.0
                             ? entry->decayed_elapsed_ms
                             : entry->average_elapsed_ms;
        if (!positive(result.mean_ms))
            return result;

        const double samples = static_cast<double>(entry->sample_count);
        const double standard_error =
            entry->standard_deviation_ms / std::sqrt(std::max(1.0, samples));
        const double half_width = std::max(0.0, config.empirical_override_z * standard_error);
        result.ci_low_ms = std::max(0.0, result.mean_ms - half_width);
        result.ci_high_ms = result.mean_ms + half_width;

        const double evidence_confidence =
            std::clamp(1.0
                           - std::exp(-samples
                                      / static_cast<double>(std::max<std::size_t>(
                                          1, config.minimum_empirical_override_samples))),
                       0.0,
                       1.0);
        const double coefficient_of_variation = entry->standard_deviation_ms / result.mean_ms;
        const double stability_confidence =
            std::clamp(1.0 / (1.0 + 2.0 * coefficient_of_variation), 0.0, 1.0);
        result.confidence = evidence_confidence * stability_confidence;
        result.available = result.confidence >= config.minimum_empirical_override_confidence;
        return result;
    }

    static void apply_empirical_plan_override(std::vector<PlanCostEstimate>& candidates,
                                              const WorkloadFingerprint& fingerprint,
                                              const Config& config)
    {
        if (candidates.empty())
            return;

        std::size_t selected_index = candidates.size();
        double selected_score = std::numeric_limits<double>::max();
        for (std::size_t index = 0; index < candidates.size(); ++index)
        {
            if (candidates[index].available && positive(candidates[index].ranking_score)
                && candidates[index].ranking_score < selected_score)
            {
                selected_score = candidates[index].ranking_score;
                selected_index = index;
            }
        }
        if (selected_index >= candidates.size())
            return;

        std::vector<EmpiricalEvidence> evidence(candidates.size());
        std::size_t empirical_best = candidates.size();
        double empirical_best_ms = std::numeric_limits<double>::max();
        for (std::size_t index = 0; index < candidates.size(); ++index)
        {
            if (!candidates[index].available)
                continue;
            evidence[index] = empirical_evidence(
                global_experience_database().find_plan(fingerprint, candidates[index].plan),
                config);
            if (!evidence[index].available)
                continue;

            candidates[index].empirical_override_candidate = true;
            candidates[index].empirical_override_confidence = evidence[index].confidence;
            candidates[index].empirical_runtime_ms = evidence[index].mean_ms;
            candidates[index].empirical_runtime_ci_low_ms = evidence[index].ci_low_ms;
            candidates[index].empirical_runtime_ci_high_ms = evidence[index].ci_high_ms;

            if (evidence[index].mean_ms < empirical_best_ms)
            {
                empirical_best_ms = evidence[index].mean_ms;
                empirical_best = index;
            }
        }

        if (empirical_best >= candidates.size() || empirical_best == selected_index
            || !evidence[selected_index].available)
        {
            return;
        }

        const double gain_percent =
            std::max(0.0,
                     (evidence[selected_index].mean_ms - evidence[empirical_best].mean_ms)
                         / evidence[empirical_best].mean_ms * 100.0);
        const bool intervals_separate =
            evidence[empirical_best].ci_high_ms < evidence[selected_index].ci_low_ms;
        const bool strong_fallback =
            gain_percent >= 2.0 * config.minimum_empirical_override_gain_percent
            && evidence[empirical_best].confidence
                   >= std::min(1.0, config.minimum_empirical_override_confidence + 0.10)
            && evidence[selected_index].confidence >= config.minimum_empirical_override_confidence;

        PlanCostEstimate& best = candidates[empirical_best];
        best.empirical_override_gain_percent = gain_percent;
        if (gain_percent < config.minimum_empirical_override_gain_percent
            || (!intervals_separate && !strong_fallback))
        {
            return;
        }

        best.empirical_override_applied = true;
        best.confidence =
            std::clamp(std::max(best.confidence, evidence[empirical_best].confidence), 0.0, 1.0);
        double minimum_score = std::numeric_limits<double>::max();
        for (const PlanCostEstimate& candidate : candidates)
        {
            if (candidate.available && positive(candidate.ranking_score))
                minimum_score = std::min(minimum_score, candidate.ranking_score);
        }
        if (positive(minimum_score))
            best.ranking_score = minimum_score * (1.0 - 1.0e-9);
    }

    static void enforce_override_guard(std::vector<PlanCostEstimate>& candidates,
                                       std::size_t baseline_index,
                                       const Config& config)
    {
        std::size_t learned_index = candidates.size();
        double best_score = std::numeric_limits<double>::max();
        for (std::size_t index = 0; index < candidates.size(); ++index)
        {
            if (candidates[index].available && positive(candidates[index].ranking_score)
                && candidates[index].ranking_score < best_score)
            {
                best_score = candidates[index].ranking_score;
                learned_index = index;
            }
        }
        if (learned_index >= candidates.size() || learned_index == baseline_index)
        {
            return;
        }

        PlanCostEstimate& baseline = candidates[baseline_index];
        PlanCostEstimate& learned = candidates[learned_index];
        learned.learned_override_candidate = true;

        const double history_confidence = std::clamp(learned.historical_evidence_confidence
                                                         * learned.historical_stability_confidence
                                                         * learned.historical_recent_consistency,
                                                     0.0,
                                                     1.0);
        learned.learned_override_confidence = std::max({learned.hierarchical_residual_confidence,
                                                        learned.residual_correction_confidence,
                                                        history_confidence});

        learned.learned_override_gain_percent =
            baseline.ranking_score > 0.0 ? std::max(0.0,
                                                    (baseline.ranking_score - learned.ranking_score)
                                                        / baseline.ranking_score * 100.0)
                                         : 0.0;
        const double uncertainty_margin =
            (baseline.predicted_runtime_stddev_ms + learned.predicted_runtime_stddev_ms)
            / std::max(1.0e-9, baseline.predicted_total_ms) * 100.0;
        learned.learned_override_required_margin_percent =
            config.minimum_learned_override_gain_percent
            + config.override_uncertainty_margin_scale * std::min(25.0, uncertainty_margin);
        learned.learned_override_allowed =
            learned.learned_override_confidence >= config.minimum_learned_override_confidence
            && learned.learned_override_gain_percent
                   >= learned.learned_override_required_margin_percent;

        if (!learned.learned_override_allowed)
        {
            baseline.learned_override_candidate = true;
            baseline.learned_override_allowed = false;
            baseline.learned_override_confidence = learned.learned_override_confidence;
            baseline.learned_override_gain_percent = learned.learned_override_gain_percent;
            baseline.learned_override_required_margin_percent =
                learned.learned_override_required_margin_percent;
            baseline.ranking_score =
                std::min(baseline.ranking_score, learned.ranking_score * (1.0 - 1.0e-9));
        }
    }

    static bool positive(double value)
    {
        return std::isfinite(value) && value > 0.0;
    }
};
} // namespace smart
