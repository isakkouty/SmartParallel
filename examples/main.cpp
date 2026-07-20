#include <atomic>
#include <cstddef>
#include <iostream>
#include <string>
#include <smart/core/config.hpp>
#include <smart/execution/backend.hpp>
#include <smart/execution/execution_context.hpp>
#include <smart/execution/nested_execution_coordinator.hpp>
#include <smart/execution/parallel.hpp>

namespace
{
bool verify_runtime(smart::ExecutionEngineType expected_type,
                    const char* expected_name,
                    const smart::RuntimeCapabilities& expected_capabilities)
{
    const smart::IExecutionEngine& runtime = smart::execution_engine(expected_type);
    const smart::RuntimeCapabilities actual = runtime.capabilities();

    const bool passed = runtime.type() == expected_type
                        && std::string(runtime.name()) == expected_name
                        && actual.supports_native_nesting
                               == expected_capabilities.supports_native_nesting
                        && actual.uses_shared_workers == expected_capabilities.uses_shared_workers
                        && actual.supports_concurrency_limit
                               == expected_capabilities.supports_concurrency_limit
                        && actual.supports_dynamic_chunks
                               == expected_capabilities.supports_dynamic_chunks;

    std::cout << "Runtime " << runtime.name() << ": " << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
}

smart::ExecutionPlan parallel_plan(smart::ExecutionEngineType engine)
{
    smart::ExecutionPlan plan;
    plan.parallel = true;
    plan.strategy = smart::ExecutionStrategy::DynamicChunks;
    plan.engine = engine;
    plan.job_count = 4;
    plan.chunk_size = 1;
    return plan;
}

bool verify_coordinator_decision(const char* label,
                                 const smart::ExecutionContext& parent,
                                 const smart::ExecutionPlan& requested_plan,
                                 smart::NestedExecutionPolicy expected_policy,
                                 bool expected_parallel)
{
    const smart::NestedExecutionDecision decision =
        smart::NestedExecutionCoordinator{}.coordinate(parent, requested_plan);
    const smart::ExecutionPlan& effective_plan = decision.plan;
    const bool expected_fallback =
        expected_policy == smart::NestedExecutionPolicy::SequentialFallback;
    const bool passed = decision.policy == expected_policy
                        && effective_plan.parallel == expected_parallel
                        && decision.uses_sequential_fallback() == expected_fallback
                        && (expected_parallel
                                || (effective_plan.strategy == smart::ExecutionStrategy::Sequential
                                    && effective_plan.job_count == 1
                                    && effective_plan.chunk_size == 0));

    std::cout << "Coordinator " << label << ": " << (passed ? "PASS" : "FAIL")
              << " [" << smart::nested_execution_policy_name(decision.policy) << "]\n";
    return passed;
}
} // namespace

int main()
{
    smart::global_config().enable_experience = false;
    smart::global_config().enable_parallel_for_auto_profiling = false;
    smart::global_config().enable_parallel_for_profile_cache = false;

    constexpr std::size_t outer_iterations = 4;
    constexpr std::size_t inner_iterations = 3;

    std::atomic<std::size_t> outer_context_checks{0};
    std::atomic<std::size_t> nested_context_checks{0};
    std::atomic<bool> failed{false};

    smart::parallel_for(
        0,
        outer_iterations,
        [&](std::size_t)
        {
            const smart::ExecutionContext outer = smart::current_execution_context();
            if (outer.loop_id == 0 || outer.parent_loop_id != 0 || outer.depth != 1
                || outer.nested())
            {
                failed.store(true, std::memory_order_relaxed);
            }
            else
            {
                outer_context_checks.fetch_add(1, std::memory_order_relaxed);
            }

            smart::parallel_for(
                0,
                inner_iterations,
                [&](std::size_t)
                {
                    const smart::ExecutionContext inner = smart::current_execution_context();
                    if (inner.loop_id == 0 || inner.loop_id == outer.loop_id
                        || inner.parent_loop_id != outer.loop_id || inner.depth != 2
                        || !inner.nested())
                    {
                        failed.store(true, std::memory_order_relaxed);
                    }
                    else
                    {
                        nested_context_checks.fetch_add(1, std::memory_order_relaxed);
                    }
                });
        });

    const std::size_t expected_nested_checks = outer_iterations * inner_iterations;
    const bool context_passed = !failed.load(std::memory_order_relaxed)
                                && outer_context_checks.load(std::memory_order_relaxed)
                                       == outer_iterations
                                && nested_context_checks.load(std::memory_order_relaxed)
                                       == expected_nested_checks
                                && !smart::inside_parallel_loop();

    std::cout << "Outer context checks: " << outer_context_checks << '/' << outer_iterations
              << '\n';
    std::cout << "Nested context checks: " << nested_context_checks << '/'
              << expected_nested_checks << '\n';
    std::cout << (context_passed ? "PASS: nested execution context detected correctly.\n"
                                : "FAIL: nested execution context is incorrect.\n");

    const bool thread_pool_passed =
        verify_runtime(smart::ExecutionEngineType::ThreadPool,
                       "thread_pool",
                       smart::RuntimeCapabilities{false, true, true, true});
    const bool one_tbb_passed = verify_runtime(smart::ExecutionEngineType::OneTbb,
                                               "one_tbb",
                                               smart::RuntimeCapabilities{true, true, true, true});
    const bool static_thread_passed =
        verify_runtime(smart::ExecutionEngineType::StaticThread,
                       "static_thread",
                       smart::RuntimeCapabilities{false, false, true, false});

    const smart::IExecutionEngine& automatic =
        smart::execution_engine(smart::ExecutionEngineType::Auto);
    const bool auto_passed = automatic.type() == smart::ExecutionEngineType::ThreadPool;
    std::cout << "Runtime auto resolution: " << (auto_passed ? "PASS" : "FAIL") << '\n';

    smart::ExecutionContext tbb_parent;
    tbb_parent.loop_id = 1;
    tbb_parent.depth = 1;
    tbb_parent.engine = smart::ExecutionEngineType::OneTbb;
    tbb_parent.parallel = true;

    smart::ExecutionContext pool_parent = tbb_parent;
    pool_parent.engine = smart::ExecutionEngineType::ThreadPool;

    smart::ExecutionContext sequential_parent = tbb_parent;
    sequential_parent.parallel = false;

    const bool tbb_native = verify_coordinator_decision(
        "oneTBB -> oneTBB",
        tbb_parent,
        parallel_plan(smart::ExecutionEngineType::OneTbb),
        smart::NestedExecutionPolicy::NativeRuntimeDelegation,
        true);
    const bool pool_fallback = verify_coordinator_decision(
        "ThreadPool -> ThreadPool",
        pool_parent,
        parallel_plan(smart::ExecutionEngineType::ThreadPool),
        smart::NestedExecutionPolicy::SequentialFallback,
        false);
    const bool cross_runtime_fallback = verify_coordinator_decision(
        "oneTBB -> ThreadPool",
        tbb_parent,
        parallel_plan(smart::ExecutionEngineType::ThreadPool),
        smart::NestedExecutionPolicy::SequentialFallback,
        false);
    const bool inactive_parent = verify_coordinator_decision(
        "sequential parent -> ThreadPool",
        sequential_parent,
        parallel_plan(smart::ExecutionEngineType::ThreadPool),
        smart::NestedExecutionPolicy::NotNested,
        true);

    const bool passed = context_passed && thread_pool_passed && one_tbb_passed
                        && static_thread_passed && auto_passed && tbb_native && pool_fallback
                        && cross_runtime_fallback && inactive_parent;

    std::cout << (passed ? "PASS: nested execution coordinator is correct.\n"
                         : "FAIL: nested execution coordinator mismatch.\n");
    return passed ? 0 : 1;
}
