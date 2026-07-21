#include <smart/execution/parallel.hpp>
#include <tbb/task_arena.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace
{
struct ConfigGuard
{
    smart::Config saved = smart::global_config();
    ~ConfigGuard() { smart::global_config() = saved; }
};

smart::ExecutionContext parent_context(smart::ExecutionEngineType engine,
                                       std::size_t budget,
                                       std::uint64_t loop_id)
{
    smart::ExecutionContext parent;
    parent.loop_id = loop_id;
    parent.depth = 1;
    parent.engine = engine;
    parent.parallel = true;
    parent.concurrency_budget = budget;
    parent.root_loop_id = loop_id;
    parent.nearest_parallel_ancestor_loop_id = loop_id;
    parent.runtime_owner_loop_id = loop_id;
    parent.root_engine = engine;
    parent.nearest_parallel_ancestor_engine = engine;
    parent.runtime_owner_engine = engine;
    parent.inherited_concurrency_budget = budget;
    return parent;
}

void configure_public_path(smart::ExecutionEngineType engine)
{
    auto& config = smart::global_config();
    config.execution_engine = engine;
    config.enable_experience = false;
    config.enable_parallel_for_profile_cache = false;
    config.enable_parallel_for_cached_sequential_fast_path = false;
    config.enable_parallel_for_auto_profiling = true;
    config.parallel_for_profile_min_samples = 4;
    config.parallel_for_profile_max_samples = 8;
    config.parallel_for_profile_min_signal_ms = 0.01;
    config.parallel_for_estimated_overhead_ms = 0.001;
    config.parallel_for_minimum_predicted_speedup = 1.01;
}

bool exactly_once(const std::vector<std::atomic<unsigned>>& visits)
{
    return std::all_of(visits.begin(), visits.end(), [](const auto& visit) {
        return visit.load(std::memory_order_relaxed) == 1;
    });
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
    ConfigGuard config_guard;
    constexpr std::size_t iterations = 128;
    const std::size_t budget = 4;

    configure_public_path(smart::ExecutionEngineType::ThreadPool);
    const auto thread_pool_parent =
        parent_context(smart::ExecutionEngineType::ThreadPool, budget, 1000);
    std::vector<std::atomic<unsigned>> thread_pool_visits(iterations);
    std::mutex thread_pool_mutex;
    std::set<std::thread::id> thread_pool_workers;
    std::atomic<std::size_t> thread_pool_active{0};
    std::atomic<std::size_t> thread_pool_max_active{0};
    {
        smart::detail::ExecutionContextScope parent_scope(thread_pool_parent);
        smart::parallel_for(0, iterations, [&](std::size_t index) {
            const std::size_t active =
                thread_pool_active.fetch_add(1, std::memory_order_relaxed) + 1;
            update_max(thread_pool_max_active, active);
            thread_pool_visits[index].fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            {
                std::lock_guard<std::mutex> lock(thread_pool_mutex);
                thread_pool_workers.insert(std::this_thread::get_id());
            }
            thread_pool_active.fetch_sub(1, std::memory_order_relaxed);
        });
    }
    const auto thread_pool_diagnostics = smart::global_last_parallel_for_nested_diagnostics();
    const bool thread_pool_passed =
        exactly_once(thread_pool_visits)
        && thread_pool_diagnostics.coordinated
        && thread_pool_diagnostics.requested_engine == smart::ExecutionEngineType::ThreadPool
        && thread_pool_diagnostics.selected_engine == smart::ExecutionEngineType::ThreadPool
        && thread_pool_diagnostics.policy == smart::NestedExecutionPolicy::CooperativeHelping
        && thread_pool_diagnostics.mechanism == smart::NestedExecutionMechanism::CooperativeHelping
        && thread_pool_diagnostics.same_runtime_domain
        && thread_pool_diagnostics.effective_budget == budget
        && thread_pool_workers.size() >= 2
        && thread_pool_max_active.load(std::memory_order_relaxed) >= 2
        && thread_pool_max_active.load(std::memory_order_relaxed) <= budget;
    std::cout << "Public parallel_for routes nested ThreadPool helping: "
              << (thread_pool_passed ? "PASS" : "FAIL")
              << " [policy=" << smart::nested_execution_policy_name(thread_pool_diagnostics.policy)
              << ", workers=" << thread_pool_workers.size()
              << ", max_active=" << thread_pool_max_active.load(std::memory_order_relaxed)
              << ", budget=" << thread_pool_diagnostics.effective_budget << "]\n";

    configure_public_path(smart::ExecutionEngineType::OneTbb);
    const auto one_tbb_parent =
        parent_context(smart::ExecutionEngineType::OneTbb, budget, 2000);
    std::vector<std::atomic<unsigned>> one_tbb_visits(iterations);
    std::mutex one_tbb_mutex;
    std::set<int> one_tbb_workers;
    std::atomic<std::size_t> one_tbb_active{0};
    std::atomic<std::size_t> one_tbb_max_active{0};
    smart::ParallelForNestedDiagnostics one_tbb_diagnostics;
    tbb::task_arena arena(static_cast<int>(budget));
    arena.execute([&] {
        smart::detail::ExecutionContextScope parent_scope(one_tbb_parent);
        smart::parallel_for(0, iterations, [&](std::size_t index) {
            const std::size_t active =
                one_tbb_active.fetch_add(1, std::memory_order_relaxed) + 1;
            update_max(one_tbb_max_active, active);
            one_tbb_visits[index].fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            {
                std::lock_guard<std::mutex> lock(one_tbb_mutex);
                one_tbb_workers.insert(tbb::this_task_arena::current_thread_index());
            }
            one_tbb_active.fetch_sub(1, std::memory_order_relaxed);
        });
        one_tbb_diagnostics = smart::global_last_parallel_for_nested_diagnostics();
    });
    const bool one_tbb_passed =
        exactly_once(one_tbb_visits)
        && one_tbb_diagnostics.coordinated
        && one_tbb_diagnostics.requested_engine == smart::ExecutionEngineType::OneTbb
        && one_tbb_diagnostics.selected_engine == smart::ExecutionEngineType::OneTbb
        && (one_tbb_diagnostics.policy == smart::NestedExecutionPolicy::NativeRuntimeDelegation
            || one_tbb_diagnostics.policy == smart::NestedExecutionPolicy::BudgetLimitedDelegation)
        && one_tbb_diagnostics.mechanism == smart::NestedExecutionMechanism::NativeDelegation
        && one_tbb_diagnostics.same_runtime_domain
        && one_tbb_diagnostics.effective_budget == budget
        && one_tbb_workers.size() >= 2
        && one_tbb_max_active.load(std::memory_order_relaxed) >= 2
        && one_tbb_max_active.load(std::memory_order_relaxed) <= budget
        && one_tbb_workers.count(tbb::task_arena::not_initialized) == 0;
    std::cout << "Public parallel_for routes nested oneTBB delegation: "
              << (one_tbb_passed ? "PASS" : "FAIL")
              << " [policy=" << smart::nested_execution_policy_name(one_tbb_diagnostics.policy)
              << ", workers=" << one_tbb_workers.size()
              << ", max_active=" << one_tbb_max_active.load(std::memory_order_relaxed)
              << ", budget=" << one_tbb_diagnostics.effective_budget << "]\n";

    configure_public_path(smart::ExecutionEngineType::StaticThread);
    const auto cross_parent =
        parent_context(smart::ExecutionEngineType::ThreadPool, budget, 3000);
    std::vector<std::atomic<unsigned>> fallback_visits(iterations);
    std::mutex fallback_mutex;
    std::set<std::thread::id> fallback_workers;
    {
        smart::detail::ExecutionContextScope parent_scope(cross_parent);
        smart::parallel_for(0, iterations, [&](std::size_t index) {
            fallback_visits[index].fetch_add(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(fallback_mutex);
            fallback_workers.insert(std::this_thread::get_id());
        });
    }
    const auto fallback_diagnostics = smart::global_last_parallel_for_nested_diagnostics();
    const bool fallback_passed =
        exactly_once(fallback_visits)
        && fallback_diagnostics.coordinated
        && fallback_diagnostics.requested_engine == smart::ExecutionEngineType::StaticThread
        && fallback_diagnostics.policy == smart::NestedExecutionPolicy::SequentialFallback
        && fallback_diagnostics.mechanism == smart::NestedExecutionMechanism::SequentialFallback
        && fallback_diagnostics.cross_backend_transition
        && fallback_diagnostics.effective_budget == 1
        && fallback_workers.size() == 1;
    std::cout << "Public parallel_for routes unsupported transition to fallback: "
              << (fallback_passed ? "PASS" : "FAIL")
              << " [policy=" << smart::nested_execution_policy_name(fallback_diagnostics.policy)
              << ", threads=" << fallback_workers.size()
              << ", budget=" << fallback_diagnostics.effective_budget << "]\n";

    const bool passed = thread_pool_passed && one_tbb_passed && fallback_passed;
    std::cout << (passed
        ? "PASS: automatic parallel_for nested integration is correct.\n"
        : "FAIL: automatic parallel_for nested integration is incorrect.\n");
    return passed ? 0 : 1;
}
