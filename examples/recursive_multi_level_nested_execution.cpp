#include <smart/execution/executor.hpp>
#include <smart/execution/nested_execution_coordinator.hpp>
#include <smart/workload/workload_builder.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace
{
smart::ExecutionPlan plan_for(smart::ExecutionEngineType engine, std::size_t jobs)
{
    smart::ExecutionPlan plan;
    plan.parallel = true;
    plan.engine = engine;
    plan.strategy = engine == smart::ExecutionEngineType::StaticThread
                        ? smart::ExecutionStrategy::StaticChunks
                        : smart::ExecutionStrategy::DynamicChunks;
    plan.job_count = jobs;
    plan.chunk_size = 1;
    return plan;
}

smart::ExecutionContext child_context(const smart::ExecutionContext& parent,
                                      const smart::NestedExecutionDecision& decision)
{
    smart::detail::ExecutionContextScope parent_scope(parent);
    smart::ExecutionContext child = smart::detail::make_execution_context();
    child.engine = decision.plan.parallel
                       ? smart::resolve_execution_engine_type(decision.plan.engine)
                       : smart::ExecutionEngineType::Auto;
    child.parallel = decision.plan.parallel;
    child.nested_policy = decision.policy;
    child.concurrency_budget = decision.effective_budget;
    smart::inherit_execution_lineage(child, parent);
    return child;
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

struct RecursiveState
{
    std::atomic<std::size_t> leaves{0};
    std::atomic<std::size_t> active{0};
    std::atomic<std::size_t> max_active{0};
    std::atomic<std::size_t> max_depth{0};
    std::atomic<bool> lineage_ok{true};
    std::atomic<bool> policies_ok{true};
};

void execute_level(const smart::ExecutionContext& parent,
                   std::size_t remaining_levels,
                   RecursiveState& state)
{
    constexpr std::size_t fanout = 2;
    smart::NestedExecutionCoordinator coordinator;
    auto decision = coordinator.coordinate(parent, plan_for(smart::ExecutionEngineType::ThreadPool, 4));

    smart::NestedExecutionConstraints constraints;
    constraints.iteration_count = fanout;
    constraints.minimum_iterations_per_worker = 1;
    constraints.minimum_chunks_per_worker = 1;
    decision = coordinator.enforce_constraints(decision, constraints);

    const smart::ExecutionContext child = child_context(parent, decision);
    update_max(state.max_depth, child.depth);
    if (child.root_loop_id != parent.root_loop_id
        || child.runtime_owner_loop_id != parent.runtime_owner_loop_id
        || child.runtime_owner_engine != smart::ExecutionEngineType::ThreadPool
        || child.concurrency_budget > parent.inherited_concurrency_budget)
        state.lineage_ok.store(false);
    if (decision.policy != smart::NestedExecutionPolicy::CooperativeHelping
        && decision.policy != smart::NestedExecutionPolicy::SequentialFallback)
        state.policies_ok.store(false);

    smart::Workload workload = smart::WorkloadBuilder::index_range(fanout);
    smart::execute_workload(workload, decision.plan, [&](std::size_t) {
        smart::detail::ExecutionContextScope scope(child);
        if (remaining_levels == 1)
        {
            const std::size_t now = state.active.fetch_add(1) + 1;
            update_max(state.max_active, now);
            state.leaves.fetch_add(1);
            state.active.fetch_sub(1);
        }
        else
        {
            execute_level(child, remaining_levels - 1, state);
        }
    }, decision.policy);
}

bool report(const char* label, bool passed, const std::string& details)
{
    std::cout << label << ": " << (passed ? "PASS" : "FAIL") << " [" << details << "]\n";
    return passed;
}
} // namespace

int main()
{
    smart::ExecutionContext root;
    root.loop_id = 12000;
    root.depth = 1;
    root.engine = smart::ExecutionEngineType::ThreadPool;
    root.parallel = true;
    root.concurrency_budget = 4;
    smart::inherit_execution_lineage(root, {});

    RecursiveState recursive;
    execute_level(root, 4, recursive);
    const bool recursive_ok = recursive.leaves.load() == 16
        && recursive.max_depth.load() == 5
        && recursive.max_active.load() <= 4
        && recursive.lineage_ok.load()
        && recursive.policies_ok.load();
    report("Four-level ThreadPool recursion preserves lineage and budget", recursive_ok,
           "leaves=" + std::to_string(recursive.leaves.load())
           + ", depth=" + std::to_string(recursive.max_depth.load())
           + ", max_active=" + std::to_string(recursive.max_active.load())
           + ", budget=4");

    smart::ExecutionContext sequential;
    {
        smart::detail::ExecutionContextScope scope(root);
        sequential = smart::detail::make_execution_context();
    }
    sequential.engine = smart::ExecutionEngineType::Auto;
    sequential.parallel = false;
    sequential.concurrency_budget = 1;
    smart::inherit_execution_lineage(sequential, root);

    auto reentry_decision = smart::NestedExecutionCoordinator{}.coordinate(
        sequential, plan_for(smart::ExecutionEngineType::ThreadPool, 4));
    const auto reentry = child_context(sequential, reentry_decision);
    const bool bridge_ok = sequential.runtime_owner_loop_id == root.loop_id
        && sequential.runtime_owner_engine == smart::ExecutionEngineType::ThreadPool
        && sequential.inherited_concurrency_budget == 4
        && reentry.root_loop_id == root.loop_id
        && reentry.runtime_owner_loop_id == root.loop_id
        && reentry.runtime_owner_engine == smart::ExecutionEngineType::ThreadPool
        && reentry.depth == 3;
    report("Sequential bridge preserves runtime ownership for re-entry", bridge_ok,
           "bridge_depth=" + std::to_string(sequential.depth)
           + ", reentry_depth=" + std::to_string(reentry.depth)
           + ", inherited_budget=" + std::to_string(reentry.inherited_concurrency_budget));

    auto cross_decision = smart::NestedExecutionCoordinator{}.coordinate(
        reentry, plan_for(smart::ExecutionEngineType::StaticThread, 4));
    const auto cross = child_context(reentry, cross_decision);
    std::atomic<std::size_t> cross_visits{0};
    std::mutex thread_mutex;
    std::set<std::thread::id> cross_threads;
    smart::Workload cross_workload = smart::WorkloadBuilder::index_range(32);
    smart::execute_workload(cross_workload, cross_decision.plan, [&](std::size_t) {
        smart::detail::ExecutionContextScope scope(cross);
        cross_visits.fetch_add(1);
        std::lock_guard<std::mutex> lock(thread_mutex);
        cross_threads.insert(std::this_thread::get_id());
    }, cross_decision.policy);
    const bool cross_ok = cross_decision.policy == smart::NestedExecutionPolicy::SequentialFallback
        && cross_decision.negotiation.cross_backend_transition
        && cross_decision.effective_budget == 1
        && cross_visits.load() == 32
        && cross_threads.size() == 1
        && cross.root_loop_id == root.loop_id;
    report("Deep mixed-backend transition remains conservative", cross_ok,
           "policy=" + std::string(smart::nested_execution_policy_name(cross_decision.policy))
           + ", visits=" + std::to_string(cross_visits.load())
           + ", threads=" + std::to_string(cross_threads.size()));

    const bool passed = recursive_ok && bridge_ok && cross_ok;
    std::cout << (passed
        ? "PASS: recursive multi-level nested execution is correct.\n"
        : "FAIL: recursive multi-level nested execution is incorrect.\n");
    return passed ? 0 : 1;
}
