#include <smart/execution/parallel.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
struct ConfigGuard
{
    smart::Config saved = smart::global_config();
    ~ConfigGuard() { smart::global_config() = saved; }
};

smart::ExecutionContext parent_context(std::size_t budget)
{
    smart::ExecutionContext parent;
    parent.loop_id = 9000;
    parent.depth = 1;
    parent.engine = smart::ExecutionEngineType::ThreadPool;
    parent.parallel = true;
    parent.concurrency_budget = budget;
    parent.root_loop_id = parent.loop_id;
    parent.nearest_parallel_ancestor_loop_id = parent.loop_id;
    parent.runtime_owner_loop_id = parent.loop_id;
    parent.root_engine = parent.engine;
    parent.nearest_parallel_ancestor_engine = parent.engine;
    parent.runtime_owner_engine = parent.engine;
    parent.inherited_concurrency_budget = budget;
    return parent;
}

smart::ExecutionPlan nested_plan(std::size_t jobs)
{
    smart::ExecutionPlan plan;
    plan.parallel = true;
    plan.engine = smart::ExecutionEngineType::ThreadPool;
    plan.strategy = smart::ExecutionStrategy::DynamicChunks;
    plan.job_count = jobs;
    plan.chunk_size = 1;
    return plan;
}

void update_max(std::atomic<std::size_t>& maximum, std::size_t value)
{
    std::size_t observed = maximum.load(std::memory_order_relaxed);
    while (observed < value
           && !maximum.compare_exchange_weak(
               observed, value, std::memory_order_relaxed, std::memory_order_relaxed))
    {
    }
}
}

int main()
{
    ConfigGuard guard;
    smart::NestedExecutionCoordinator coordinator;
    const auto parent = parent_context(8);

    smart::NestedExecutionConstraints tiny_constraints;
    tiny_constraints.iteration_count = 7;
    tiny_constraints.minimum_iterations_per_worker = 8;
    tiny_constraints.minimum_chunks_per_worker = 1;
    const auto tiny = coordinator.enforce_constraints(
        coordinator.coordinate(parent, nested_plan(8)), tiny_constraints);
    const bool tiny_passed =
        tiny.policy == smart::NestedExecutionPolicy::SequentialFallback
        && !tiny.plan.parallel
        && tiny.effective_budget == 1
        && tiny.granularity_limited
        && tiny.granularity_budget == 1;
    std::cout << "Tiny nested work becomes sequential fallback: "
              << (tiny_passed ? "PASS" : "FAIL")
              << " [iterations=7, policy=" << smart::nested_execution_policy_name(tiny.policy)
              << ", budget=" << tiny.effective_budget << "]\n";

    smart::NestedExecutionConstraints medium_constraints;
    medium_constraints.iteration_count = 24;
    medium_constraints.minimum_iterations_per_worker = 8;
    medium_constraints.minimum_chunks_per_worker = 1;
    const auto medium = coordinator.enforce_constraints(
        coordinator.coordinate(parent, nested_plan(8)), medium_constraints);
    const bool medium_passed =
        medium.plan.parallel
        && medium.effective_budget == 3
        && medium.plan.job_count == 3
        && medium.granularity_limited
        && medium.budget_limited;
    std::cout << "Nested concurrency is clamped by useful work: "
              << (medium_passed ? "PASS" : "FAIL")
              << " [iterations=24, requested=8, granularity=3, budget="
              << medium.effective_budget << "]\n";

    auto& config = smart::global_config();
    config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    config.enable_experience = false;
    config.enable_parallel_for_profile_cache = false;
    config.enable_parallel_for_cached_sequential_fast_path = false;
    config.enable_parallel_for_auto_profiling = true;
    config.parallel_for_profile_min_samples = 4;
    config.parallel_for_profile_max_samples = 4;
    config.parallel_for_profile_min_signal_ms = 0.001;
    config.parallel_for_estimated_overhead_ms = 0.001;
    config.parallel_for_minimum_predicted_speedup = 1.01;
    config.enable_nested_granularity_enforcement = true;
    config.nested_min_iterations_per_worker = 8;
    config.nested_min_chunks_per_worker = 1;

    constexpr std::size_t iterations = 28;
    std::vector<std::atomic<unsigned>> visits(iterations);
    std::atomic<std::size_t> active{0};
    std::atomic<std::size_t> max_active{0};
    {
        smart::detail::ExecutionContextScope parent_scope(parent);
        smart::parallel_for(0, iterations, [&](std::size_t index) {
            const std::size_t now = active.fetch_add(1, std::memory_order_relaxed) + 1;
            update_max(max_active, now);
            visits[index].fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::microseconds(150));
            active.fetch_sub(1, std::memory_order_relaxed);
        });
    }
    const auto diagnostics = smart::global_last_parallel_for_nested_diagnostics();
    const bool exact_once = std::all_of(visits.begin(), visits.end(), [](const auto& value) {
        return value.load(std::memory_order_relaxed) == 1;
    });
    const bool public_passed =
        exact_once
        && diagnostics.coordinated
        && diagnostics.policy == smart::NestedExecutionPolicy::CooperativeHelping
        && diagnostics.granularity_limited
        && diagnostics.effective_budget == 3
        && diagnostics.granularity_budget == 3
        && max_active.load(std::memory_order_relaxed) >= 2
        && max_active.load(std::memory_order_relaxed) <= 3;
    std::cout << "Public parallel_for enforces the refined nested budget: "
              << (public_passed ? "PASS" : "FAIL")
              << " [iterations=" << iterations
              << ", max_active=" << max_active.load(std::memory_order_relaxed)
              << ", granularity=" << diagnostics.granularity_budget
              << ", budget=" << diagnostics.effective_budget << "]\n";

    const bool passed = tiny_passed && medium_passed && public_passed;
    std::cout << (passed
        ? "PASS: granularity and concurrency-budget enforcement are correct.\n"
        : "FAIL: granularity and concurrency-budget enforcement are incorrect.\n");
    return passed ? 0 : 1;
}
