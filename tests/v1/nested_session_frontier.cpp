#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <smart/execution/parallel.hpp>

namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

struct ConfigGuard
{
    smart::Config saved = smart::global_config();
    ~ConfigGuard() { smart::global_config() = saved; }
};

constexpr std::array<std::size_t, 4> extents{2, 3, 4, 64};
constexpr std::size_t leaf_count = extents[0] * extents[1] * extents[2] * extents[3];

std::size_t flatten(std::size_t i, std::size_t j, std::size_t k, std::size_t l)
{
    return (((i * extents[1]) + j) * extents[2] + k) * extents[3] + l;
}

void burn(std::size_t seed)
{
    volatile std::uint64_t value = static_cast<std::uint64_t>(seed + 1);
    for (std::size_t round = 0; round < 96; ++round)
        value = (value ^ (value >> 13)) * 0x9E3779B97F4A7C15ull + round;
    (void)value;
}

void run_nested(std::vector<std::atomic<unsigned>>& visits)
{
    smart::parallel_for(0, extents[0], [&](std::size_t i)
    {
        smart::parallel_for(0, extents[1], [&](std::size_t j)
        {
            smart::parallel_for(0, extents[2], [&](std::size_t k)
            {
                smart::parallel_for(0, extents[3], [&](std::size_t l)
                {
                    const std::size_t index = flatten(i, j, k, l);
                    visits[index].fetch_add(1, std::memory_order_relaxed);
                    burn(index);
                });
            });
        });
    });
}

void reset_visits(std::vector<std::atomic<unsigned>>& visits)
{
    for (auto& visit : visits)
        visit.store(0, std::memory_order_relaxed);
}

void require_exactly_once(const std::vector<std::atomic<unsigned>>& visits)
{
    for (const auto& visit : visits)
        require(visit.load(std::memory_order_relaxed) == 1,
                "nested automatic execution skipped or duplicated a leaf");
}

void test_frontier_and_root_budget()
{
    ConfigGuard guard;
    auto& config = smart::global_config();
    config.enable_experience = false;
    config.enable_utility_model_runtime = false;
    config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    config.enable_parallel_for_auto_profiling = true;
    config.enable_parallel_for_profile_cache = true;
    config.parallel_for_profile_cache_min_hits = 1;
    config.parallel_for_sequential_fast_path_min_observations = 1;
    config.enable_nested_execution_session = true;
    config.nested_root_concurrency_budget = 4;
    config.enable_nested_parallel_frontier = true;
    config.enable_nested_frontier_deferral = true;
    config.enable_nested_frontier_promotion = true;
    config.enable_nested_online_telemetry = true;
    config.nested_min_parallel_work_ms = 0.001;
    config.nested_target_chunk_ms = 0.02;
    config.nested_plan_hysteresis = 1.0;
    config.enable_nested_execution_trace = true;

    smart::global_function_profile_cache().clear();
    smart::clear_nested_execution_trace();

    std::vector<std::atomic<unsigned>> visits(leaf_count);
    reset_visits(visits);
    run_nested(visits); // cold exactly-once telemetry and cache population
    require_exactly_once(visits);
    const auto learning_trace = smart::nested_execution_trace_snapshot();
    bool root_learned_without_sampling_plan = false;
    bool learning_descendants_sequential = false;
    for (const auto& record : learning_trace)
    {
        if (record.depth == 1 && record.decision_reason == "root_online_cold_learning"
            && !record.parallel)
            root_learned_without_sampling_plan = true;
        if (record.depth > 1 && record.decision_reason == "conservative_learning_descendant"
            && !record.parallel)
            learning_descendants_sequential = true;
    }
    require(root_learned_without_sampling_plan,
            "cold root did not use post-execution online telemetry");
    require(learning_descendants_sequential,
            "cold root learning allowed a provisional descendant frontier");

    reset_visits(visits);
    smart::clear_nested_execution_trace();
    run_nested(visits); // cached policy should select a stable L3 frontier
    require_exactly_once(visits);

    const auto trace = smart::nested_execution_trace_snapshot();
    require(!trace.empty(), "nested trace was not recorded");

    bool deferred_l1 = false;
    bool deferred_l2 = false;
    bool promoted_l3 = false;
    bool sequential_l4 = false;
    std::size_t maximum_leases = 0;
    for (const auto& record : trace)
    {
        maximum_leases = std::max(maximum_leases, record.max_root_leased_workers);
        if (record.depth == 1 && record.decision_reason == "defer_underfilled_outer_level")
            deferred_l1 = true;
        if (record.depth == 2 && record.decision_reason == "defer_underfilled_outer_level")
            deferred_l2 = true;
        if (record.depth == 3 && record.decision_reason == "promote_parallel_frontier"
            && record.parallel && record.effective_budget == 4)
            promoted_l3 = true;
        if (record.depth == 4
            && record.decision_reason == "frontier_descendant_fast_path"
            && !record.parallel)
            sequential_l4 = true;
    }

    require(deferred_l1, "automatic policy did not defer level 1");
    require(deferred_l2, "automatic policy did not defer level 2");
    require(promoted_l3, "automatic policy did not promote level 3 as the frontier");
    require(sequential_l4, "automatic policy did not suppress scheduling below the frontier");
    require(maximum_leases <= 4, "root session exceeded its four-worker lease budget");
}

void test_parallel_for_nd_exactly_once()
{
    ConfigGuard guard;
    auto& config = smart::global_config();
    config.enable_experience = false;
    config.enable_utility_model_runtime = false;
    config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    config.nested_root_concurrency_budget = 4;
    config.enable_nested_execution_trace = false;
    smart::global_function_profile_cache().clear();

    std::vector<std::atomic<unsigned>> visits(leaf_count);
    reset_visits(visits);
    smart::parallel_for_nd(extents, [&](std::size_t i, std::size_t j, std::size_t k, std::size_t l)
    {
        const std::size_t index = flatten(i, j, k, l);
        visits[index].fetch_add(1, std::memory_order_relaxed);
        burn(index);
    });
    require_exactly_once(visits);
}
} // namespace

int main()
{
    try
    {
        test_frontier_and_root_budget();
        test_parallel_for_nd_exactly_once();
        std::cout << "nested session/frontier validation: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "nested session/frontier validation: FAIL: " << error.what() << '\n';
        return 1;
    }
}
