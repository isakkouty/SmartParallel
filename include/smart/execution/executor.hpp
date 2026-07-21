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
                                NestedExecutionPolicy nested_policy = NestedExecutionPolicy::NotNested)
{
    std::size_t total = workload.iterations;

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

        static_thread_execute_range(total,
                                    plan.job_count,
                                    [&](std::size_t i)
                                    {
                                        func(i);
                                    });

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
        request.function = [&](std::size_t i)
        {
            func(i);
        };
        execution_backend(plan.engine).execute(std::move(request));
    }

    return ExecutionStats{total, timer.elapsed_ms()};
}
} // namespace smart
