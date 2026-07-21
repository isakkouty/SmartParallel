#include <smart/execution/execution_context.hpp>
#include <smart/execution/executor.hpp>
#include <smart/execution/nested_execution_coordinator.hpp>
#include <smart/execution/thread_pool.hpp>
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
smart::ExecutionPlan thread_pool_plan(std::size_t jobs, std::size_t chunk_size)
{
    smart::ExecutionPlan plan;
    plan.parallel = true;
    plan.strategy = smart::ExecutionStrategy::DynamicChunks;
    plan.engine = smart::ExecutionEngineType::ThreadPool;
    plan.job_count = jobs;
    plan.chunk_size = chunk_size;
    return plan;
}
}

int main()
{
    smart::ThreadPool& pool = smart::global_thread_pool();
    const std::size_t budget = std::max<std::size_t>(2, std::min<std::size_t>(4, pool.thread_count()));

    smart::ExecutionContext parent;
    parent.loop_id = 100;
    parent.depth = 1;
    parent.engine = smart::ExecutionEngineType::ThreadPool;
    parent.parallel = true;
    parent.concurrency_budget = budget;
    parent.root_loop_id = parent.loop_id;
    parent.runtime_owner_loop_id = parent.loop_id;
    parent.root_engine = parent.engine;
    parent.runtime_owner_engine = parent.engine;
    parent.inherited_concurrency_budget = budget;

    const smart::ExecutionPlan requested = thread_pool_plan(budget, 3);
    const smart::NestedExecutionDecision decision =
        smart::NestedExecutionCoordinator{}.coordinate(parent, requested);

    const bool negotiation_passed =
        decision.policy == smart::NestedExecutionPolicy::CooperativeHelping
        && decision.negotiation.mechanism == smart::NestedExecutionMechanism::CooperativeHelping
        && decision.plan.parallel
        && decision.plan.engine == smart::ExecutionEngineType::ThreadPool
        && decision.plan.job_count == budget
        && decision.effective_budget == budget
        && decision.uses_cooperative_helping();

    std::cout << "ThreadPool negotiation activates cooperative helping: "
              << (negotiation_passed ? "PASS" : "FAIL")
              << " [policy=" << smart::nested_execution_policy_name(decision.policy)
              << ", budget=" << decision.effective_budget << "]\n";

    constexpr std::size_t iterations = 96;
    std::vector<std::atomic<unsigned>> visits(iterations);
    for (auto& visit : visits)
        visit.store(0, std::memory_order_relaxed);

    std::mutex worker_mutex;
    std::set<std::thread::id> workers;
    std::atomic<bool> outer_finished{false};

    pool.submit(
        [&]()
        {
            smart::detail::ExecutionContextScope parent_scope(parent);
            const smart::Workload workload = smart::WorkloadBuilder::index_range(iterations);
            smart::execute_workload(
                workload,
                decision.plan,
                [&](std::size_t index)
                {
                    visits[index].fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    std::lock_guard<std::mutex> lock(worker_mutex);
                    workers.insert(std::this_thread::get_id());
                },
                decision.policy);
            outer_finished.store(true, std::memory_order_release);
        });

    pool.wait();

    bool exactly_once = outer_finished.load(std::memory_order_acquire);
    for (const auto& visit : visits)
        exactly_once = exactly_once && visit.load(std::memory_order_relaxed) == 1;

    const bool execution_passed = exactly_once && workers.size() >= 2
                                  && workers.size() <= budget
                                  && workers.size() <= pool.thread_count();

    std::cout << "ThreadPool backend executes nested scheduler-visible work: "
              << (execution_passed ? "PASS" : "FAIL")
              << " [iterations=" << iterations << ", workers=" << workers.size()
              << ", budget=" << budget << "]\n";

    const bool completed_without_deadlock = outer_finished.load(std::memory_order_acquire);
    std::cout << "Nested ThreadPool worker completes without pool-wait deadlock: "
              << (completed_without_deadlock ? "PASS" : "FAIL") << '\n';

    const bool passed = negotiation_passed && execution_passed && completed_without_deadlock;
    std::cout << (passed ? "PASS: ThreadPool backend integration is correct.\n"
                        : "FAIL: ThreadPool backend integration is incorrect.\n");
    return passed ? 0 : 1;
}
