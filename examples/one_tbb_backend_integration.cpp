#include <smart/execution/backend.hpp>
#include <smart/execution/execution_context.hpp>
#include <smart/execution/executor.hpp>
#include <smart/execution/nested_execution_coordinator.hpp>
#include <smart/workload/workload_builder.hpp>

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
smart::ExecutionPlan one_tbb_plan(std::size_t jobs, std::size_t chunk_size)
{
    smart::ExecutionPlan plan;
    plan.parallel = true;
    plan.strategy = smart::ExecutionStrategy::DynamicChunks;
    plan.engine = smart::ExecutionEngineType::OneTbb;
    plan.job_count = jobs;
    plan.chunk_size = chunk_size;
    return plan;
}
}

int main()
{
    const std::size_t budget = std::max<std::size_t>(2, std::min<std::size_t>(
        4, static_cast<std::size_t>(tbb::this_task_arena::max_concurrency())));

    smart::ExecutionContext parent;
    parent.loop_id = 200;
    parent.depth = 1;
    parent.engine = smart::ExecutionEngineType::OneTbb;
    parent.parallel = true;
    parent.concurrency_budget = budget;
    parent.root_loop_id = parent.loop_id;
    parent.runtime_owner_loop_id = parent.loop_id;
    parent.root_engine = parent.engine;
    parent.runtime_owner_engine = parent.engine;
    parent.inherited_concurrency_budget = budget;

    const smart::ExecutionPlan requested = one_tbb_plan(budget, 2);
    const smart::NestedExecutionDecision decision =
        smart::NestedExecutionCoordinator{}.coordinate(parent, requested);

    const bool negotiation_passed =
        decision.policy == smart::NestedExecutionPolicy::NativeRuntimeDelegation
        && decision.negotiation.mechanism == smart::NestedExecutionMechanism::NativeDelegation
        && decision.plan.parallel
        && decision.plan.engine == smart::ExecutionEngineType::OneTbb
        && decision.effective_budget == budget;

    std::cout << "oneTBB negotiation activates native delegation: "
              << (negotiation_passed ? "PASS" : "FAIL")
              << " [policy=" << smart::nested_execution_policy_name(decision.policy)
              << ", budget=" << decision.effective_budget << "]\n";

    constexpr std::size_t iterations = 128;
    std::vector<std::atomic<unsigned>> direct_visits(iterations);
    for (auto& visit : direct_visits)
        visit.store(0, std::memory_order_relaxed);

    std::mutex direct_mutex;
    std::set<int> direct_workers;
    smart::BackendExecutionResult direct_result;

    tbb::task_arena arena(static_cast<int>(budget));
    arena.execute(
        [&]()
        {
            smart::detail::ExecutionContextScope parent_scope(parent);
            smart::BackendExecutionRequest request;
            request.total = iterations;
            request.concurrency_budget = decision.effective_budget;
            request.chunk_size = decision.plan.chunk_size;
            request.native_delegation = true;
            request.function = [&](std::size_t index)
            {
                direct_visits[index].fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                const int worker = tbb::this_task_arena::current_thread_index();
                std::lock_guard<std::mutex> lock(direct_mutex);
                direct_workers.insert(worker);
            };
            direct_result = smart::execution_backend(smart::ExecutionEngineType::OneTbb)
                                .execute(std::move(request));
        });

    bool direct_exactly_once = true;
    for (const auto& visit : direct_visits)
        direct_exactly_once = direct_exactly_once
                              && visit.load(std::memory_order_relaxed) == 1;

    const bool domain_reuse_passed = direct_exactly_once
                                     && direct_result.executed
                                     && direct_result.native_delegation
                                     && direct_result.reused_runtime_domain
                                     && direct_result.runtime_concurrency == budget
                                     && direct_workers.size() >= 2
                                     && direct_workers.size() <= budget
                                     && direct_workers.count(tbb::task_arena::not_initialized) == 0;

    std::cout << "oneTBB backend reuses the active task arena: "
              << (domain_reuse_passed ? "PASS" : "FAIL")
              << " [iterations=" << iterations
              << ", workers=" << direct_workers.size()
              << ", arena=" << direct_result.runtime_concurrency << "]\n";

    std::vector<std::atomic<unsigned>> routed_visits(iterations);
    for (auto& visit : routed_visits)
        visit.store(0, std::memory_order_relaxed);

    std::mutex routed_mutex;
    std::set<int> routed_workers;
    arena.execute(
        [&]()
        {
            smart::detail::ExecutionContextScope parent_scope(parent);
            const smart::Workload workload = smart::WorkloadBuilder::index_range(iterations);
            smart::execute_workload(
                workload,
                decision.plan,
                [&](std::size_t index)
                {
                    routed_visits[index].fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    const int worker = tbb::this_task_arena::current_thread_index();
                    std::lock_guard<std::mutex> lock(routed_mutex);
                    routed_workers.insert(worker);
                },
                decision.policy);
        });

    bool routed_exactly_once = true;
    for (const auto& visit : routed_visits)
        routed_exactly_once = routed_exactly_once
                              && visit.load(std::memory_order_relaxed) == 1;

    const bool executor_routing_passed = routed_exactly_once
                                         && routed_workers.size() >= 2
                                         && routed_workers.size() <= budget
                                         && routed_workers.count(tbb::task_arena::not_initialized) == 0;

    std::cout << "Nested executor routes oneTBB work through native delegation: "
              << (executor_routing_passed ? "PASS" : "FAIL")
              << " [iterations=" << iterations
              << ", workers=" << routed_workers.size()
              << ", budget=" << budget << "]\n";

    const bool passed = negotiation_passed && domain_reuse_passed && executor_routing_passed;
    std::cout << (passed ? "PASS: oneTBB backend integration is correct.\n"
                        : "FAIL: oneTBB backend integration is incorrect.\n");
    return passed ? 0 : 1;
}
