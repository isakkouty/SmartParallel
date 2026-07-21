#include <smart/execution/executor.hpp>
#include <smart/execution/nested_execution_coordinator.hpp>
#include <smart/workload/workload_builder.hpp>

#include <atomic>
#include <cstddef>
#include <iostream>
#include <string>

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

bool report(const char* label, bool passed, const std::string& details)
{
    std::cout << label << ": " << (passed ? "PASS" : "FAIL")
              << " [" << details << "]\n";
    return passed;
}

struct DeepState
{
    std::atomic<std::size_t> leaves{0};
    std::atomic<std::size_t> max_depth{0};
    std::atomic<bool> lineage_ok{true};
    std::atomic<bool> policy_ok{true};
};

void update_max(std::atomic<std::size_t>& target, std::size_t value)
{
    std::size_t current = target.load(std::memory_order_relaxed);
    while (current < value
           && !target.compare_exchange_weak(current, value,
                                            std::memory_order_relaxed,
                                            std::memory_order_relaxed))
    {
    }
}

void recurse(const smart::ExecutionContext& parent,
             smart::ExecutionEngineType engine,
             std::size_t remaining_levels,
             DeepState& state)
{
    constexpr std::size_t fanout = 2;
    smart::NestedExecutionCoordinator coordinator;
    auto decision = coordinator.coordinate(parent, plan_for(engine, parent.concurrency_budget));

    smart::NestedExecutionConstraints constraints;
    constraints.iteration_count = fanout;
    decision = coordinator.enforce_constraints(decision, constraints);

    const smart::ExecutionContext child = child_context(parent, decision);
    update_max(state.max_depth, child.depth);

    if (child.root_loop_id != parent.root_loop_id
        || child.runtime_owner_loop_id != parent.runtime_owner_loop_id
        || child.runtime_owner_engine != parent.runtime_owner_engine
        || child.inherited_concurrency_budget > parent.inherited_concurrency_budget
        || child.concurrency_budget > parent.inherited_concurrency_budget)
        state.lineage_ok.store(false, std::memory_order_relaxed);

    if (engine == smart::ExecutionEngineType::ThreadPool)
    {
        if (decision.policy != smart::NestedExecutionPolicy::CooperativeHelping
            && decision.policy != smart::NestedExecutionPolicy::SequentialFallback)
            state.policy_ok.store(false, std::memory_order_relaxed);
    }
    else if (engine == smart::ExecutionEngineType::OneTbb)
    {
        if (decision.policy != smart::NestedExecutionPolicy::NativeRuntimeDelegation
            && decision.policy != smart::NestedExecutionPolicy::BudgetLimitedDelegation
            && decision.policy != smart::NestedExecutionPolicy::SequentialFallback)
            state.policy_ok.store(false, std::memory_order_relaxed);
    }

    const smart::Workload workload = smart::WorkloadBuilder::index_range(fanout);
    smart::execute_workload(workload, decision.plan, [&](std::size_t) {
        smart::detail::ExecutionContextScope scope(child);
        if (remaining_levels == 1)
            state.leaves.fetch_add(1, std::memory_order_relaxed);
        else
            recurse(child, engine, remaining_levels - 1, state);
    }, decision.policy);
}
} // namespace

int main()
{
    constexpr std::size_t budget = 4;

    smart::ExecutionContext thread_pool_root;
    thread_pool_root.loop_id = 25000;
    thread_pool_root.depth = 1;
    thread_pool_root.engine = smart::ExecutionEngineType::ThreadPool;
    thread_pool_root.parallel = true;
    thread_pool_root.concurrency_budget = budget;
    smart::inherit_execution_lineage(thread_pool_root, {});

    DeepState thread_pool_state;
    recurse(thread_pool_root, smart::ExecutionEngineType::ThreadPool, 6, thread_pool_state);
    const bool thread_pool_ok = thread_pool_state.leaves.load() == 64
        && thread_pool_state.max_depth.load() == 7
        && thread_pool_state.lineage_ok.load()
        && thread_pool_state.policy_ok.load();
    report("Six-level ThreadPool nesting remains exact and bounded", thread_pool_ok,
           "leaves=" + std::to_string(thread_pool_state.leaves.load())
           + ", depth=" + std::to_string(thread_pool_state.max_depth.load())
           + ", budget=" + std::to_string(budget));

    smart::ExecutionContext tbb_root;
    tbb_root.loop_id = 26000;
    tbb_root.depth = 1;
    tbb_root.engine = smart::ExecutionEngineType::OneTbb;
    tbb_root.parallel = true;
    tbb_root.concurrency_budget = budget;
    smart::inherit_execution_lineage(tbb_root, {});

    DeepState tbb_state;
    recurse(tbb_root, smart::ExecutionEngineType::OneTbb, 5, tbb_state);
    const bool tbb_ok = tbb_state.leaves.load() == 32
        && tbb_state.max_depth.load() == 6
        && tbb_state.lineage_ok.load()
        && tbb_state.policy_ok.load();
    report("Five-level oneTBB nesting preserves its runtime domain", tbb_ok,
           "leaves=" + std::to_string(tbb_state.leaves.load())
           + ", depth=" + std::to_string(tbb_state.max_depth.load())
           + ", budget=" + std::to_string(budget));

    smart::NestedExecutionCoordinator coordinator;
    auto static_fallback = coordinator.coordinate(
        thread_pool_root, plan_for(smart::ExecutionEngineType::StaticThread, budget));
    const auto static_child = child_context(thread_pool_root, static_fallback);

    auto thread_pool_reentry = coordinator.coordinate(
        static_child, plan_for(smart::ExecutionEngineType::ThreadPool, budget));
    const auto reentry_child = child_context(static_child, thread_pool_reentry);

    auto tbb_fallback = coordinator.coordinate(
        reentry_child, plan_for(smart::ExecutionEngineType::OneTbb, budget));
    const auto tbb_fallback_child = child_context(reentry_child, tbb_fallback);

    std::atomic<std::size_t> mixed_visits{0};
    const smart::Workload mixed_workload = smart::WorkloadBuilder::index_range(40);
    smart::execute_workload(mixed_workload, tbb_fallback.plan, [&](std::size_t) {
        smart::detail::ExecutionContextScope scope(tbb_fallback_child);
        mixed_visits.fetch_add(1, std::memory_order_relaxed);
    }, tbb_fallback.policy);

    const bool mixed_ok = static_fallback.policy == smart::NestedExecutionPolicy::SequentialFallback
        && static_child.runtime_owner_engine == smart::ExecutionEngineType::ThreadPool
        && thread_pool_reentry.policy == smart::NestedExecutionPolicy::NotNested
        && reentry_child.runtime_owner_engine == smart::ExecutionEngineType::ThreadPool
        && tbb_fallback.policy == smart::NestedExecutionPolicy::SequentialFallback
        && tbb_fallback.negotiation.cross_backend_transition
        && tbb_fallback.effective_budget == 1
        && mixed_visits.load() == 40
        && tbb_fallback_child.root_loop_id == thread_pool_root.loop_id;
    report("Mixed-backend chain preserves lineage and conservative fallbacks", mixed_ok,
           "static=" + std::string(smart::nested_execution_policy_name(static_fallback.policy))
           + ", reentry=" + std::string(smart::nested_execution_policy_name(thread_pool_reentry.policy))
           + ", tbb=" + std::string(smart::nested_execution_policy_name(tbb_fallback.policy))
           + ", visits=" + std::to_string(mixed_visits.load()));

    bool stress_ok = true;
    std::size_t stress_leaves = 0;
    for (std::size_t round = 0; round < 12; ++round)
    {
        DeepState state;
        recurse(thread_pool_root, smart::ExecutionEngineType::ThreadPool, 4, state);
        stress_leaves += state.leaves.load();
        stress_ok = stress_ok && state.leaves.load() == 16
            && state.lineage_ok.load() && state.policy_ok.load();
    }
    report("Repeated deep nesting completes without leaked or stranded work", stress_ok,
           "rounds=12, leaves=" + std::to_string(stress_leaves));

    const bool passed = thread_pool_ok && tbb_ok && mixed_ok && stress_ok;
    std::cout << (passed
        ? "PASS: deep nesting and mixed-backend validation is correct.\n"
        : "FAIL: deep nesting and mixed-backend validation is incorrect.\n");
    return passed ? 0 : 1;
}
