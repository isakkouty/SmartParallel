#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <smart/core/config.hpp>
#include <smart/decision/plan_prediction.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/workload/fingerprint.hpp>
#include <smart/workload/workload_family.hpp>

namespace smart
{
struct ResidualCorrectionResult
{
    bool applied = false;
    bool exact_history_used = false;
    bool similarity_history_used = false;
    bool family_aware = false;
    double factor = 1.0;
    double confidence = 0.0;

    double uncertainty = 1.0;
    double history_weight = 0.0;
    double similarity = 0.0;
    double family_weight_scale = 1.0;
    double effective_minimum_factor = 1.0;
    double effective_maximum_factor = 1.0;

    std::size_t samples = 0;
};

class ResidualCorrectionPolicy
{
  public:
    ResidualCorrectionResult evaluate(const PlanCostEstimate& estimate,
                                      const WorkloadFingerprint& fingerprint) const
    {
        ResidualCorrectionResult result;
        if (!estimate.available || !positive(estimate.predicted_total_ms))
            return result;

        const Config& config = effective_config();
        const ExperienceDatabase& database = global_experience_database();
        const ExperienceEntry* exact = database.find_plan(fingerprint, estimate.plan);

        const FamilyLimits family =
            family_limits(estimate.workload_family, estimate.workload_family_confidence, config);
        result.family_aware = family.applied;
        result.family_weight_scale = family.weight_scale;
        result.effective_minimum_factor = family.minimum_factor;
        result.effective_maximum_factor = family.maximum_factor;

        double requested_log_factor = 0.0;
        double evidence_weight = 0.0;

        const bool phase11_exact_mode =
            config.enable_hierarchical_residual_learning && estimate.residual_features.available;
        const std::size_t minimum_exact_samples = phase11_exact_mode
                                                      ? config.minimum_exact_plan_residual_samples
                                                      : config.minimum_residual_correction_samples;
        const std::size_t full_exact_samples =
            phase11_exact_mode ? config.exact_plan_residual_full_confidence_samples
                               : config.residual_full_confidence_samples;

        if (exact != nullptr && exact->prediction_sample_count >= minimum_exact_samples)
        {
            const double sample_strength =
                std::clamp(static_cast<double>(exact->prediction_sample_count)
                               / static_cast<double>(std::max<std::size_t>(1, full_exact_samples)),
                           0.0,
                           1.0);
            const double relative_noise =
                exact->average_elapsed_ms > 0.0
                    ? exact->standard_deviation_ms / exact->average_elapsed_ms
                    : 1.0;
            const double stability = std::clamp(1.0 - relative_noise, 0.0, 1.0);
            const double prediction_quality = std::clamp(
                1.0 - exact->average_absolute_prediction_error_percent / 150.0, 0.15, 1.0);

            result.uncertainty = std::clamp(1.0 - sample_strength * stability, 0.0, 1.0);
            result.confidence =
                std::clamp(sample_strength * stability * prediction_quality, 0.0, 1.0);

            const double exact_factor = std::clamp(
                exact->average_runtime_correction, family.minimum_factor, family.maximum_factor);
            requested_log_factor = std::log(exact_factor);
            evidence_weight = result.confidence;
            result.samples = exact->prediction_sample_count;
            result.exact_history_used = true;
        }

        if (!phase11_exact_mode && config.enable_residual_similarity_transfer
            && (!result.exact_history_used
                || result.confidence < config.minimum_residual_exact_confidence))
        {
            const SimilarExperienceSummary similar =
                database.similar_plan_summary(fingerprint, estimate.plan);
            if (similar.available && positive(similar.elapsed_ms)
                && similar.similarity >= config.minimum_similarity)
            {
                const double similar_factor = std::clamp(
                    similar.elapsed_ms / estimate.predicted_total_ms,
                    std::max(family.minimum_factor, config.minimum_similarity_residual_factor),
                    std::min(family.maximum_factor, config.maximum_similarity_residual_factor));
                const double similarity_weight =
                    std::clamp(similar.confidence * similar.similarity
                                   * config.maximum_residual_similarity_weight,
                               0.0,
                               config.maximum_residual_similarity_weight);

                const double similar_log_factor = std::log(similar_factor);
                requested_log_factor = evidence_weight > 0.0
                                           ? (requested_log_factor * evidence_weight
                                              + similar_log_factor * similarity_weight)
                                                 / (evidence_weight + similarity_weight)
                                           : similar_log_factor;
                evidence_weight += similarity_weight;
                result.similarity_history_used = similarity_weight > 0.0;
                result.similarity = similar.similarity;
                result.confidence =
                    std::clamp(result.confidence + similarity_weight * 0.5, 0.0, 1.0);
                result.uncertainty = std::clamp(1.0 - result.confidence, 0.0, 1.0);
            }
        }

        const double maximum_weight =
            std::clamp(config.maximum_residual_correction_weight, 0.0, 1.0);
        const double minimum_exact_confidence =
            phase11_exact_mode ? config.minimum_exact_plan_residual_confidence : 0.0;
        result.history_weight =
            result.confidence >= minimum_exact_confidence
                ? std::clamp(evidence_weight * family.weight_scale, 0.0, maximum_weight)
                : 0.0;

        // Blend in log space. This treats equal relative over- and
        // under-prediction symmetrically and avoids an arithmetic bias
        // toward large upward corrections.
        result.factor = std::exp(requested_log_factor * result.history_weight);
        result.factor = std::clamp(result.factor, family.minimum_factor, family.maximum_factor);
        result.applied = result.history_weight > 0.0 && std::abs(result.factor - 1.0) > 1e-9;
        return result;
    }

    void apply(PlanCostEstimate& estimate, const WorkloadFingerprint& fingerprint) const
    {
        const ResidualCorrectionResult result = evaluate(estimate, fingerprint);

        estimate.residual_correction_applied = result.applied;
        estimate.residual_exact_history_used = result.exact_history_used;
        estimate.residual_similarity_used = result.similarity_history_used;
        estimate.residual_correction_factor = result.factor;
        estimate.residual_correction_confidence = result.confidence;
        estimate.residual_correction_uncertainty = result.uncertainty;
        estimate.residual_history_weight = result.history_weight;
        estimate.residual_similarity = result.similarity;
        estimate.residual_samples = result.samples;
        estimate.residual_family_aware = result.family_aware;
        estimate.residual_family_weight_scale = result.family_weight_scale;
        estimate.residual_effective_minimum_factor = result.effective_minimum_factor;
        estimate.residual_effective_maximum_factor = result.effective_maximum_factor;

        if (!result.applied)
            return;

        estimate.pre_residual_total_ms = estimate.predicted_total_ms;
        const double runtime_without_framework =
            std::max(0.0, estimate.predicted_total_ms - estimate.framework_overhead_ms);
        estimate.predicted_total_ms =
            runtime_without_framework * result.factor + estimate.framework_overhead_ms;
        estimate.predicted_runtime_stddev_ms *= result.factor;

        // Preserve component ownership. Exact-plan history is a final
        // observed-total correction, not a re-estimation of every latent
        // execution component.
        estimate.confidence = std::clamp(
            std::max(estimate.confidence, result.confidence * (1.0 - result.uncertainty * 0.5)),
            0.0,
            1.0);
    }

  private:
    struct FamilyLimits
    {
        bool applied = false;
        double weight_scale = 1.0;
        double minimum_factor = 0.70;
        double maximum_factor = 1.40;
    };

    static FamilyLimits
    family_limits(WorkloadFamily family, double family_confidence, const Config& config)
    {
        FamilyLimits result;
        result.minimum_factor = config.minimum_residual_correction_factor;
        result.maximum_factor = config.maximum_residual_correction_factor;

        if (!config.enable_family_aware_residual_correction)
            return result;

        const double confidence = std::clamp(family_confidence, 0.0, 1.0);
        double target_weight = config.unknown_residual_weight_scale;
        double target_minimum = 0.90;
        double target_maximum = 1.10;

        switch (family)
        {
            case WorkloadFamily::ComputeHeavy:
                target_weight = config.compute_residual_weight_scale;
                target_minimum = 0.78;
                target_maximum = 1.28;
                break;
            case WorkloadFamily::StreamingMemory:
                target_weight = config.streaming_residual_weight_scale;
                target_minimum = 0.86;
                target_maximum = 1.16;
                break;
            case WorkloadFamily::IrregularMemory:
                target_weight = config.irregular_residual_weight_scale;
                target_minimum = 0.82;
                target_maximum = 1.22;
                break;
            case WorkloadFamily::BranchHeavy:
                target_weight = config.branch_residual_weight_scale;
                target_minimum = 0.84;
                target_maximum = 1.20;
                break;
            case WorkloadFamily::Mixed:
                target_weight = config.mixed_residual_weight_scale;
                target_minimum = 0.88;
                target_maximum = 1.14;
                break;
            case WorkloadFamily::Unknown:
            default:
                target_weight = config.unknown_residual_weight_scale;
                break;
        }

        // Low-confidence classifications approach a neutral, conservative
        // family policy instead of abruptly switching correction regimes.
        result.weight_scale = std::clamp(1.0 + (target_weight - 1.0) * confidence, 0.0, 1.0);
        result.minimum_factor = std::clamp(
            config.minimum_residual_correction_factor
                + (target_minimum - config.minimum_residual_correction_factor) * confidence,
            config.minimum_residual_correction_factor,
            1.0);
        result.maximum_factor = std::clamp(
            config.maximum_residual_correction_factor
                + (target_maximum - config.maximum_residual_correction_factor) * confidence,
            1.0,
            config.maximum_residual_correction_factor);
        result.applied = confidence > 0.0;
        return result;
    }

    static bool positive(double value)
    {
        return std::isfinite(value) && value > 0.0;
    }
};
} // namespace smart
