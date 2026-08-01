#pragma once

#include <cstddef>
#include <smart/core/config.hpp>
#include <smart/core/statistics.hpp>
#include <smart/core/timing_scope.hpp>
#include <smart/decision/decision.hpp>
#include <smart/execution/backend.hpp>
#include <smart/execution/static_thread_engine.hpp>
#include <utility>

namespace smart
{
template <typename Function>
ExecutionStats execute_workload(const Workload& workload,
                                const ExecutionPlan& plan,
                                Function func,
                                NestedExecutionPolicy nested_policy = NestedExecutionPolicy::NotNested,
                                const ExecutionContext* execution_context = nullptr)
{
    std::size_t total = workload.iterations;
    const ExecutionContext backend_context =
        execution_context == nullptr ? current_execution_context() : *execution_context;

    if (total == 0)
    {
        return ExecutionStats{0, 0.0};
    }

    Timer timer;

    if (!plan.parallel || plan.strategy == ExecutionStrategy::Sequential)
    {
        TimingScope scope("execution_sequential");

        if (nested_policy == NestedExecutionPolicy::SequentialFallback
            && plan.engine != ExecutionEngineType::Auto)
        {
            BackendExecutionRequest request;
            request.total = total;
            request.concurrency_budget = 1;
            request.sequential_fallback = true;
            request.loop_id = backend_context.loop_id;
            request.nested_session = backend_context.nested_session;
            request.function = [&](std::size_t i)
            {
                func(i);
            };
            execution_backend(plan.engine).execute(std::move(request));
        }
        else
        {
            for (std::size_t i = 0; i < total; ++i)
                func(i);
        }

        return ExecutionStats{total, timer.elapsed_ms()};
    }

    if (plan.strategy == ExecutionStrategy::StaticChunks)
    {
        TimingScope scope("execution_static_chunks");

        BackendExecutionRequest request;
        request.total = total;
        request.concurrency_budget = plan.job_count;
        request.chunk_size = plan.chunk_size;
        request.native_delegation =
            nested_policy == NestedExecutionPolicy::NativeRuntimeDelegation
            || nested_policy == NestedExecutionPolicy::BudgetLimitedDelegation;
        request.cooperative_helping =
            nested_policy == NestedExecutionPolicy::CooperativeHelping;
        request.sequential_fallback =
            nested_policy == NestedExecutionPolicy::SequentialFallback;
        request.loop_id = backend_context.loop_id;
        request.nested_session = backend_context.nested_session;
        request.function = [&](std::size_t i)
        {
            func(i);
        };
        const ExecutionEngineType backend_type = plan.engine == ExecutionEngineType::Auto
            ? ExecutionEngineType::StaticThread
            : plan.engine;
        execution_backend(backend_type).execute(std::move(request));

        return ExecutionStats{total, timer.elapsed_ms()};
    }

    if (plan.strategy == ExecutionStrategy::DynamicChunks)
    {
        TimingScope scope("execution_dynamic_chunks");

        BackendExecutionRequest request;
        request.total = total;
        request.concurrency_budget = plan.job_count;
        request.chunk_size = plan.chunk_size;
        request.native_delegation =
            nested_policy == NestedExecutionPolicy::NativeRuntimeDelegation
            || nested_policy == NestedExecutionPolicy::BudgetLimitedDelegation;
        request.cooperative_helping =
            nested_policy == NestedExecutionPolicy::CooperativeHelping;
        request.loop_id = backend_context.loop_id;
        request.nested_session = backend_context.nested_session;
        request.function = [&](std::size_t i)
        {
            func(i);
        };
        execution_backend(plan.engine).execute(std::move(request));

        return ExecutionStats{total, timer.elapsed_ms()};
    }

    {
        TimingScope scope("execution_backend_fallback");

        BackendExecutionRequest request;
        request.total = total;
        request.concurrency_budget = plan.job_count;
        request.chunk_size = plan.chunk_size;
        request.native_delegation =
            nested_policy == NestedExecutionPolicy::NativeRuntimeDelegation
            || nested_policy == NestedExecutionPolicy::BudgetLimitedDelegation;
        request.cooperative_helping =
            nested_policy == NestedExecutionPolicy::CooperativeHelping;
        request.loop_id = backend_context.loop_id;
        request.nested_session = backend_context.nested_session;
        request.function = [&](std::size_t i)
        {
            func(i);
        };
        execution_backend(plan.engine).execute(std::move(request));
    }

    return ExecutionStats{total, timer.elapsed_ms()};
}
} // namespace smart
