#pragma once

#include <algorithm>
#include <smart/execution/runtime_capabilities.hpp>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <mutex>
#include <optional>
#include <smart/core/config.hpp>
#include <smart/decision/decision_rules.hpp>
#include <smart/ranking/utility_model_artifact.hpp>
#include <string>
#include <vector>

namespace smart::ranking
{
struct RuntimeUtilityDecision
{
    bool model_loaded = false;
    bool model_promoted = false;
    bool model_compatible = false;
    bool applied = false;
    double confidence = 0.0;
    std::string reason;

    ExecutionPlan plan;
};

inline std::string plan_backend_name(const ExecutionPlan& plan)
{
    if (!plan.parallel || plan.strategy == ExecutionStrategy::Sequential)
        return "Sequential";
    if (plan.engine == ExecutionEngineType::OneTbb)
        return "oneTBB";
    if (plan.engine == ExecutionEngineType::ThreadPool
        || plan.engine == ExecutionEngineType::StaticThread)
        return "ThreadPool";
    return "Unknown";
}

inline std::string plan_schedule_name(const ExecutionPlan& plan)
{
    if (!plan.parallel || plan.strategy == ExecutionStrategy::Sequential)
        return "Sequential";
    if (plan.strategy == ExecutionStrategy::StaticChunks)
        return "StaticChunks";
    return "DynamicChunks";
}

inline std::vector<double> make_runtime_raw_features(const DecisionContext& context,
                                                     const ExecutionHints& hints,
                                                     const ExecutionPlan& plan)
{
    const double logical_iterations = static_cast<double>(context.workload.iterations);

    const FunctionProfile* profile = context.function_profile;
    const bool profile_available = profile != nullptr && profile->available;

    const double jobs = static_cast<double>(std::max<std::size_t>(1, plan.job_count));
    const double chunk = static_cast<double>(std::max<std::size_t>(1, plan.chunk_size));

    std::vector<double> values = {
        std::log1p(std::max(0.0, logical_iterations)),
        profile_available ? 1.0 : 0.0,
        std::log1p(std::max(0.0, profile_available ? profile->median_ms_per_iteration : 0.0)
                   * 1.0e6),
        profile_available ? profile->coefficient_of_variation : 0.0,
        profile_available ? profile->tail_ratio : 0.0,
        profile_available ? profile->parallel_worthiness : 0.0,
        profile_available ? profile->regional_cost_ratio : 0.0,
        hints.available ? 1.0 : 0.0,
        hints.arithmetic_intensity,
        hints.branchiness,
        hints.memory_randomness,
        hints.vectorization_potential,
        hints.dependency_depth,
        std::log1p(std::max(0.0, hints.bytes_touched_per_iteration)),
        std::log1p(static_cast<double>(hints.external_working_set_bytes)),
        hints.feature_confidence,
        std::log1p(jobs),
        std::log1p(chunk),
        std::log1p(std::max(1.0, logical_iterations) / jobs),
        std::log1p(std::max(1.0, logical_iterations) / chunk)};

    const std::string backend = plan_backend_name(plan);
    const std::string schedule = plan_schedule_name(plan);
    const char* backends[] = {"Sequential", "ThreadPool", "oneTBB", "OpenMP"};
    const char* schedules[] = {"Sequential", "DynamicChunks", "StaticChunks", "Guided"};
    std::vector<double> backend_flags;
    for (const char* name : backends)
        backend_flags.push_back(backend == name ? 1.0 : 0.0);
    for (double flag : backend_flags)
        values.push_back(flag);
    for (const char* name : schedules)
        values.push_back(schedule == name ? 1.0 : 0.0);

    const std::vector<double> context_values(values.begin(), values.begin() + 16);
    for (double flag : backend_flags)
        for (double value : context_values)
            values.push_back(flag * value);
    return values;
}

class RuntimeUtilityPolicy
{
  public:
    RuntimeUtilityDecision choose(const DecisionContext& context,
                                  const ExecutionHints& hints,
                                  const DecisionReport& analytical) const
    {
        RuntimeUtilityDecision result;
        result.plan = analytical.plan;

        const Config& config = effective_config();
        if (!config.enable_utility_model_runtime)
        {
            result.reason = "runtime utility model disabled";
            return result;
        }

        std::optional<UtilityModelArtifact> artifact;
        try
        {
            artifact = load_utility_model_artifact(config.utility_model_file_path);
        }
        catch (const std::exception& error)
        {
            result.reason = error.what();
            return result;
        }

        result.model_loaded = true;
        result.model_promoted = artifact->promoted();
        if (artifact->feature_schema != "phase1_utility_v1")
        {
            result.reason = "utility-model feature schema mismatch";
            return result;
        }
        result.model_compatible = true;
        if (!artifact->promoted())
        {
            result.reason = "utility model is shadow-only";
            return result;
        }

        std::vector<ExecutionPlan> candidates;
        candidates.push_back(analytical.plan);
        ExecutionPlan sequential;
        sequential.parallel = false;
        sequential.strategy = ExecutionStrategy::Sequential;
        sequential.engine = ExecutionEngineType::ThreadPool;
        sequential.job_count = 1;
        candidates.push_back(sequential);

        if (context.workload.iterations > 1)
        {
            ExecutionPlan thread_pool = analytical.plan;
            thread_pool.parallel = true;
            thread_pool.engine = ExecutionEngineType::ThreadPool;
            thread_pool.strategy = ExecutionStrategy::DynamicChunks;
            thread_pool.job_count = std::max<std::size_t>(2, analytical.plan.job_count);
            candidates.push_back(thread_pool);

            if (execution_backend_available(ExecutionEngineType::OneTbb))
            {
                ExecutionPlan one_tbb = thread_pool;
                one_tbb.engine = ExecutionEngineType::OneTbb;
                candidates.push_back(one_tbb);
            }
        }

        std::vector<double> scores;
        for (const auto& candidate : candidates)
        {
            const auto raw = make_runtime_raw_features(context, hints, candidate);
            const auto transformed = artifact->transform(raw);
            scores.push_back(artifact->model.score(transformed));
        }

        std::vector<std::size_t> order(scores.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(),
                  order.end(),
                  [&](std::size_t a, std::size_t b)
                  {
                      return scores[a] < scores[b];
                  });

        const double margin = order.size() > 1 ? scores[order[1]] - scores[order[0]] : 0.0;
        result.confidence = 1.0 - std::exp(-std::max(0.0, margin));
        if (result.confidence < config.minimum_utility_model_confidence)
        {
            result.reason = "utility-model confidence below threshold";
            return result;
        }

        result.plan = candidates[order[0]];
        result.applied = true;
        result.reason = "promoted utility model selected runtime plan";
        return result;
    }
};
} // namespace smart::ranking
