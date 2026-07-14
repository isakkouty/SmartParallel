#include <cassert>
#include <cmath>
#include <vector>

#include <smart/core/config.hpp>
#include <smart/decision/confidence_model.hpp>

int main()
{
    using namespace smart;

    const Config saved = global_config();
    global_config().enable_confidence_model = true;
    global_config().enable_confidence_aware_ranking = true;
    global_config().maximum_confidence_risk_penalty = 0.20;

    PlanCostEstimate strong;
    strong.available = true;
    strong.confidence = 0.90;
    strong.workload_family_confidence = 0.90;
    strong.residual_correction_applied = true;
    strong.residual_correction_confidence = 0.90;
    strong.experience_rank_used = true;
    strong.historical_evidence_confidence = 0.90;
    strong.historical_stability_confidence = 0.90;
    strong.historical_prediction_reliability = 0.90;
    strong.historical_recent_consistency = 0.90;
    strong.analytical_rank_score = 1.0;
    strong.ranking_score = 1.02;

    PlanCostEstimate weak = strong;
    weak.confidence = 0.35;
    weak.workload_family_confidence = 0.30;
    weak.residual_correction_confidence = 0.25;
    weak.historical_evidence_confidence = 0.20;
    weak.historical_stability_confidence = 0.25;
    weak.historical_prediction_reliability = 0.30;
    weak.historical_recent_consistency = 0.20;
    weak.ranking_score = 0.98; // slightly faster before confidence risk

    const auto strong_assessment = ConfidenceModel::assess_candidate(strong);
    const auto weak_assessment = ConfidenceModel::assess_candidate(weak);
    assert(strong_assessment.combined_confidence > weak_assessment.combined_confidence);
    assert(strong_assessment.uncertainty_penalty < weak_assessment.uncertainty_penalty);

    std::vector<PlanCostEstimate> candidates{strong, weak};
    ConfidenceModel::apply(candidates);

    assert(candidates[0].model_confidence > candidates[1].model_confidence);
    assert(candidates[1].ranking_score > 0.98);
    assert(candidates[0].decision_margin_confidence >= 0.0);
    assert(candidates[0].decision_margin_confidence <= 1.0);
    assert(std::isfinite(candidates[0].confidence));
    assert(std::isfinite(candidates[1].confidence));

    global_config() = saved;
    return 0;
}
