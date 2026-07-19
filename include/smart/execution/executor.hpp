#pragma once

#include <cstddef>
#include <smart/core/config.hpp>
#include <smart/core/statistics.hpp>
#include <smart/core/timing_scope.hpp>
#include <smart/decision/decision.hpp>
#include <smart/execution/backend.hpp>
#include <smart/execution/static_thread_engine.hpp>

namespace smart
{
template <typename Function>
ExecutionStats execute_workload(const Workload& workload, const ExecutionPlan& plan, Function func)
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

        for (std::size_t i = 0; i < total; ++i)
        {
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

        execution_engine(plan.engine)
            .execute_range(total,
                           plan.job_count,
                           plan.chunk_size,
                           [&](std::size_t i)
                           {
                               func(i);
                           });

        return ExecutionStats{total, timer.elapsed_ms()};
    }

    {
        TimingScope scope("execution_backend_fallback");

        execution_engine(plan.engine)
            .execute_range(total,
                           plan.job_count,
                           plan.chunk_size,
                           [&](std::size_t i)
                           {
                               func(i);
                           });
    }

    return ExecutionStats{total, timer.elapsed_ms()};
}
} // namespace smart
