#include <smart/execution/backend.hpp>
#include <smart/execution/execution_context.hpp>
#include <smart/execution/executor.hpp>
#include <smart/execution/nested_execution_coordinator.hpp>
#include <smart/workload/workload_builder.hpp>

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
smart::ExecutionPlan static_plan(std::size_t jobs)
{
    smart::ExecutionPlan plan;
    plan.parallel = true;
    plan.strategy = smart::ExecutionStrategy::StaticChunks;
    plan.engine = smart::ExecutionEngineType::StaticThread;
    plan.job_count = jobs;
    return plan;
}

smart::ExecutionPlan dynamic_static_plan(std::size_t jobs)
{
    smart::ExecutionPlan plan = static_plan(jobs);
    plan.strategy = smart::ExecutionStrategy::DynamicChunks;
    return plan;
}
}

int main()
{
    constexpr std::size_t budget = 4;

    smart::ExecutionContext static_parent;
    static_parent.loop_id = 200;
    static_parent.depth = 1;
    static_parent.engine = smart::ExecutionEngineType::StaticThread;
    static_parent.parallel = true;
    static_parent.concurrency_budget = budget;
    static_parent.root_loop_id = static_parent.loop_id;
    static_parent.runtime_owner_loop_id = static_parent.loop_id;
    static_parent.root_engine = static_parent.engine;
    static_parent.runtime_owner_engine = static_parent.engine;
    static_parent.inherited_concurrency_budget = budget;

    const smart::NestedExecutionDecision same_backend =
        smart::NestedExecutionCoordinator{}.coordinate(static_parent, dynamic_static_plan(budget));
    const bool negotiation_passed =
        same_backend.policy == smart::NestedExecutionPolicy::SequentialFallback
        && same_backend.negotiation.mechanism == smart::NestedExecutionMechanism::SequentialFallback
        && !same_backend.plan.parallel
        && same_backend.effective_budget == 1;

    std::cout << "Nested StaticThread resolves explicit sequential fallback: "
              << (negotiation_passed ? "PASS" : "FAIL")
              << " [policy=" << smart::nested_execution_policy_name(same_backend.policy)
              << ", budget=" << same_backend.effective_budget << "]\n";

    constexpr std::size_t iterations = 80;
    std::vector<std::atomic<unsigned>> visits(iterations);
    for (auto& visit : visits)
        visit.store(0, std::memory_order_relaxed);

    const std::thread::id caller = std::this_thread::get_id();
    std::mutex ids_mutex;
    std::set<std::thread::id> ids;
    {
        smart::detail::ExecutionContextScope scope(static_parent);
        smart::execute_workload(
            smart::WorkloadBuilder::index_range(iterations),
            same_backend.plan,
            [&](std::size_t i)
            {
                visits[i].fetch_add(1, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lock(ids_mutex);
                ids.insert(std::this_thread::get_id());
            },
            same_backend.policy);
    }

    bool exactly_once = true;
    for (const auto& visit : visits)
        exactly_once = exactly_once && visit.load(std::memory_order_relaxed) == 1;
    const bool inline_fallback_passed = exactly_once && ids.size() == 1 && *ids.begin() == caller;

    std::cout << "StaticThread nested fallback stays on the caller thread: "
              << (inline_fallback_passed ? "PASS" : "FAIL")
              << " [iterations=" << iterations << ", threads=" << ids.size() << "]\n";

    smart::BackendExecutionRequest root_request;
    root_request.total = 96;
    root_request.concurrency_budget = budget;
    std::mutex root_mutex;
    std::set<std::thread::id> root_workers;
    std::atomic<std::size_t> root_visits{0};
    root_request.function = [&](std::size_t)
    {
        root_visits.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(root_mutex);
            root_workers.insert(std::this_thread::get_id());
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    };
    const smart::BackendExecutionResult root_result =
        smart::execution_backend(smart::ExecutionEngineType::StaticThread).execute(std::move(root_request));

    const bool root_static_passed = root_result.executed
                                    && !root_result.sequential_fallback
                                    && root_result.effective_budget == budget
                                    && root_result.runtime_concurrency == budget
                                    && root_result.spawned_workers == budget
                                    && root_visits.load(std::memory_order_relaxed) == 96
                                    && root_workers.size() == budget;

    std::cout << "Root StaticThread execution uses a bounded static team: "
              << (root_static_passed ? "PASS" : "FAIL")
              << " [iterations=" << root_visits.load(std::memory_order_relaxed)
              << ", workers=" << root_workers.size() << ", budget=" << budget << "]\n";

    smart::ExecutionContext thread_pool_parent = static_parent;
    thread_pool_parent.loop_id = 300;
    thread_pool_parent.engine = smart::ExecutionEngineType::ThreadPool;
    thread_pool_parent.root_loop_id = thread_pool_parent.loop_id;
    thread_pool_parent.runtime_owner_loop_id = thread_pool_parent.loop_id;
    thread_pool_parent.root_engine = thread_pool_parent.engine;
    thread_pool_parent.runtime_owner_engine = thread_pool_parent.engine;

    const smart::NestedExecutionDecision cross_backend =
        smart::NestedExecutionCoordinator{}.coordinate(thread_pool_parent, dynamic_static_plan(budget));
    const bool cross_backend_passed =
        cross_backend.policy == smart::NestedExecutionPolicy::SequentialFallback
        && cross_backend.negotiation.cross_backend_transition
        && cross_backend.negotiation.mechanism == smart::NestedExecutionMechanism::SequentialFallback
        && cross_backend.effective_budget == 1;

    std::cout << "ThreadPool to StaticThread transition stays conservative: "
              << (cross_backend_passed ? "PASS" : "FAIL")
              << " [cross=" << cross_backend.negotiation.cross_backend_transition
              << ", budget=" << cross_backend.effective_budget << "]\n";

    smart::BackendExecutionRequest explicit_fallback;
    explicit_fallback.total = 32;
    explicit_fallback.concurrency_budget = budget;
    explicit_fallback.sequential_fallback = true;
    std::set<std::thread::id> fallback_workers;
    std::mutex fallback_mutex;
    std::atomic<std::size_t> fallback_visits{0};
    explicit_fallback.function = [&](std::size_t)
    {
        fallback_visits.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(fallback_mutex);
        fallback_workers.insert(std::this_thread::get_id());
    };
    const smart::BackendExecutionResult fallback_result =
        smart::execution_backend(smart::ExecutionEngineType::StaticThread).execute(
            std::move(explicit_fallback));
    const bool result_diagnostics_passed = fallback_result.executed
                                           && fallback_result.sequential_fallback
                                           && fallback_result.effective_budget == 1
                                           && fallback_result.runtime_concurrency == 1
                                           && fallback_result.spawned_workers == 0
                                           && fallback_visits.load(std::memory_order_relaxed) == 32
                                           && fallback_workers.size() == 1
                                           && *fallback_workers.begin() == caller;

    std::cout << "Fallback diagnostics report caller-only execution: "
              << (result_diagnostics_passed ? "PASS" : "FAIL")
              << " [runtime=" << fallback_result.runtime_concurrency
              << ", spawned=" << fallback_result.spawned_workers << "]\n";

    const bool passed = negotiation_passed && inline_fallback_passed && root_static_passed
                        && cross_backend_passed && result_diagnostics_passed;
    std::cout << (passed ? "PASS: StaticThread and fallback strategies are correct.\n"
                        : "FAIL: StaticThread and fallback strategies are incorrect.\n");
    return passed ? 0 : 1;
}
