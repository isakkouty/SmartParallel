#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <smart/core/config.hpp>
#include <smart/decision/plan_prediction.hpp>
#include <smart/workload/fingerprint.hpp>
#include <unordered_map>
#include <vector>

namespace smart
{
struct ExplorationDecision
{
    bool explored = false;
    ExecutionPlan exploitation_plan;
    ExecutionPlan selected_plan;
    double exploitation_score = 0.0;
    double selected_score = 0.0;

    double candidate_gap_percent = 0.0;
    std::size_t eligible_candidates = 0;
    std::size_t cooldown_remaining = 0;
};

class OnlineExplorationPolicy
{
  public:
    ExplorationDecision select(const WorkloadFingerprint& fingerprint,
                               const std::vector<PlanCostEstimate>& candidates,
                               const ExecutionPlan& recommended_plan,
                               double prediction_confidence)
    {
        ExplorationDecision result;
        result.exploitation_plan = recommended_plan;
        result.selected_plan = recommended_plan;

        const PlanCostEstimate* best = find_candidate(candidates, recommended_plan);
        if (best == nullptr)
            return result;

        result.exploitation_score =
            best->ranking_score > 0.0 ? best->ranking_score : best->predicted_total_ms;
        result.selected_score = result.exploitation_score;

        const Config& config = effective_config();
        // Do not create process-lifetime state when exploration is disabled.
        if (!config.enable_online_exploration || candidates.size() < 2
            || prediction_confidence < config.minimum_exploration_confidence)
        {
            return result;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        State& state = state_for(fingerprint.value);
        if (state.cooldown_remaining > 0)
        {
            --state.cooldown_remaining;
            result.cooldown_remaining = state.cooldown_remaining;
            return result;
        }

        std::vector<const PlanCostEstimate*> eligible;
        eligible.reserve(candidates.size());
        const double max_ratio =
            1.0 + std::max(0.0, config.maximum_exploration_score_gap_percent) / 100.0;

        for (const PlanCostEstimate& candidate : candidates)
        {
            if (!candidate.available || same_plan(candidate.plan, recommended_plan))
                continue;

            const double score = candidate.ranking_score > 0.0 ? candidate.ranking_score
                                                               : candidate.predicted_total_ms;
            if (!std::isfinite(score) || score <= 0.0)
                continue;
            if (score > result.exploitation_score * max_ratio)
                continue;
            if (candidate.confidence < config.minimum_exploration_candidate_confidence)
                continue;
            eligible.push_back(&candidate);
        }

        result.eligible_candidates = eligible.size();
        if (eligible.empty())
            return result;

        saturating_increment(state.opportunities);
        const double probability =
            std::clamp(config.exploration_probability, 0.0, config.maximum_exploration_probability);
        if (!deterministic_trial(fingerprint.value, state.opportunities, probability))
            return result;

        const std::size_t index = state.exploration_attempts % eligible.size();
        const PlanCostEstimate& selected = *eligible[index];
        saturating_increment(state.exploration_attempts);

        result.explored = true;
        result.selected_plan = selected.plan;
        result.selected_score =
            selected.ranking_score > 0.0 ? selected.ranking_score : selected.predicted_total_ms;
        result.candidate_gap_percent = result.exploitation_score > 0.0
            ? (result.selected_score - result.exploitation_score) / result.exploitation_score * 100.0
            : 0.0;
        return result;
    }

    void record_result(const WorkloadFingerprint& fingerprint,
                       bool explored,
                       double actual_ms,
                       double exploitation_expected_ms)
    {
        if (!explored || !effective_config().enable_online_exploration)
            return;

        std::lock_guard<std::mutex> lock(mutex_);
        State& state = state_for(fingerprint.value);
        saturating_increment(state.completed_experiments);

        if (!std::isfinite(actual_ms) || actual_ms < 0.0 || !std::isfinite(exploitation_expected_ms)
            || exploitation_expected_ms <= 0.0)
        {
            return;
        }

        const double regret = std::max(
            0.0, (actual_ms - exploitation_expected_ms) / exploitation_expected_ms * 100.0);
        state.last_exploration_regret_percent = regret;

        if (regret > effective_config().maximum_exploration_regret_percent)
        {
            saturating_increment(state.harmful_experiments);
            state.cooldown_remaining = effective_config().exploration_cooldown_calls;
        }
        else
        {
            saturating_increment(state.safe_experiments);
        }
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        states_.clear();
        access_epoch_ = 0;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return states_.size();
    }

    std::size_t cooldown_remaining(const WorkloadFingerprint& fingerprint) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = states_.find(fingerprint.value);
        return it == states_.end() ? 0 : it->second.cooldown_remaining;
    }

  private:
    struct State
    {
        std::size_t opportunities = 0;
        std::size_t exploration_attempts = 0;
        std::size_t completed_experiments = 0;
        std::size_t safe_experiments = 0;
        std::size_t harmful_experiments = 0;
        std::size_t cooldown_remaining = 0;
        std::uint64_t last_access_epoch = 0;
        double last_exploration_regret_percent = 0.0;
    };

    static void saturating_increment(std::size_t& value) noexcept
    {
        if (value != std::numeric_limits<std::size_t>::max())
            ++value;
    }

    State& state_for(std::size_t fingerprint)
    {
        const auto existing = states_.find(fingerprint);
        if (existing != states_.end())
        {
            touch(existing->second);
            return existing->second;
        }

        const std::size_t maximum = runtime_limits::bounded_limit(
            effective_config().online_exploration_state_max_entries,
            runtime_limits::exploration_states);
        while (states_.size() >= maximum)
        {
            auto victim = states_.end();
            for (auto it = states_.begin(); it != states_.end(); ++it)
            {
                if (victim == states_.end()
                    || it->second.last_access_epoch < victim->second.last_access_epoch)
                    victim = it;
            }
            if (victim == states_.end())
                break;
            states_.erase(victim);
        }
        State& state = states_[fingerprint];
        touch(state);
        return state;
    }

    void touch(State& state) noexcept
    {
        if (access_epoch_ == std::numeric_limits<std::uint64_t>::max())
        {
            std::uint64_t next = 1;
            for (auto& item : states_)
                item.second.last_access_epoch = next++;
            access_epoch_ = next;
        }
        else
        {
            ++access_epoch_;
        }
        state.last_access_epoch = access_epoch_;
    }

    static bool same_plan(const ExecutionPlan& left, const ExecutionPlan& right)
    {
        return left.parallel == right.parallel && left.engine == right.engine
               && left.strategy == right.strategy && left.job_count == right.job_count
               && left.chunk_size == right.chunk_size;
    }

    static const PlanCostEstimate* find_candidate(const std::vector<PlanCostEstimate>& candidates,
                                                  const ExecutionPlan& plan)
    {
        for (const PlanCostEstimate& candidate : candidates)
        {
            if (candidate.available && same_plan(candidate.plan, plan))
                return &candidate;
        }
        return nullptr;
    }

    static bool deterministic_trial(std::size_t fingerprint,
                                    std::size_t sequence,
                                    double probability)
    {
        if (probability <= 0.0)
            return false;
        if (probability >= 1.0)
            return true;

        std::uint64_t x = static_cast<std::uint64_t>(fingerprint)
                          ^ (static_cast<std::uint64_t>(sequence) * 0x9E3779B97F4A7C15ull);
        x ^= x >> 30;
        x *= 0xBF58476D1CE4E5B9ull;
        x ^= x >> 27;
        x *= 0x94D049BB133111EBull;
        x ^= x >> 31;
        const double unit = static_cast<double>(x >> 11) / static_cast<double>(1ull << 53);
        return unit < probability;
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::size_t, State> states_;
    std::uint64_t access_epoch_ = 0;
};

namespace detail
{
OnlineExplorationPolicy* active_runtime_online_exploration_policy() noexcept;
}

inline OnlineExplorationPolicy& global_online_exploration_policy()
{
    if (auto* policy = detail::active_runtime_online_exploration_policy())
        return *policy;
    static OnlineExplorationPolicy policy;
    return policy;
}
} // namespace smart
