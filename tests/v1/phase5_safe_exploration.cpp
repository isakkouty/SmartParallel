#include <smart/core/config.hpp>
#include <smart/decision/exploration_policy.hpp>

#include <cassert>
#include <vector>

namespace
{
    smart::PlanCostEstimate candidate(
        smart::ExecutionEngineType engine,
        double score,
        double confidence)
    {
        smart::PlanCostEstimate value;
        value.available = true;
        value.plan.parallel = true;
        value.plan.engine = engine;
        value.plan.strategy = smart::ExecutionStrategy::DynamicChunks;
        value.plan.job_count = 8;
        value.plan.chunk_size = 64;
        value.ranking_score = score;
        value.predicted_total_ms = score;
        value.confidence = confidence;
        return value;
    }
}

int main()
{
    smart::Config& config = smart::global_config();
    config.enable_online_exploration = true;
    config.exploration_probability = 1.0;
    config.maximum_exploration_probability = 1.0;
    config.maximum_exploration_score_gap_percent = 10.0;
    config.minimum_exploration_confidence = 0.5;
    config.minimum_exploration_candidate_confidence = 0.3;
    config.maximum_exploration_regret_percent = 5.0;
    config.exploration_cooldown_calls = 3;

    smart::global_online_exploration_policy().clear();

    smart::WorkloadFingerprint fingerprint;
    fingerprint.value = 12345;

    std::vector<smart::PlanCostEstimate> candidates;
    candidates.push_back(candidate(
        smart::ExecutionEngineType::ThreadPool, 1.0, 0.9));
    candidates.push_back(candidate(
        smart::ExecutionEngineType::OneTbb, 1.05, 0.8));
    candidates.push_back(candidate(
        smart::ExecutionEngineType::StaticThread, 1.50, 0.9));

    const smart::ExecutionPlan recommended = candidates.front().plan;
    const smart::ExplorationDecision first =
        smart::global_online_exploration_policy().select(
            fingerprint, candidates, recommended, 0.9);

    assert(first.explored);
    assert(first.eligible_candidates == 1);
    assert(first.selected_plan.engine == smart::ExecutionEngineType::OneTbb);

    smart::global_online_exploration_policy().record_result(
        fingerprint,
        true,
        120.0,
        100.0);

    const smart::ExplorationDecision cooled =
        smart::global_online_exploration_policy().select(
            fingerprint, candidates, recommended, 0.9);
    assert(!cooled.explored);
    assert(cooled.cooldown_remaining == 2);

    config.enable_online_exploration = false;
    return 0;
}
