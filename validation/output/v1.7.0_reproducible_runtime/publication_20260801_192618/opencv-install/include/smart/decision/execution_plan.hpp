#pragma once

#include <cstddef>
#include <smart/core/config.hpp>

namespace smart
{
inline const char* engine_name(ExecutionEngineType engine)
{
    switch (engine)
    {
        case ExecutionEngineType::ThreadPool:
            return "ThreadPool";

        case ExecutionEngineType::OneTbb:
            return "oneTBB";

        case ExecutionEngineType::StaticThread:
            return "StaticThread";

        case ExecutionEngineType::Auto:
            return "Auto";

        default:
            return "Unknown";
    }
}

enum class ExecutionStrategy
{
    Sequential,
    StaticChunks,
    DynamicChunks
};

struct ExecutionPlan
{
    ExecutionStrategy strategy = ExecutionStrategy::Sequential;
    ExecutionEngineType engine = ExecutionEngineType::ThreadPool;

    bool parallel = false;

    // Maximum parallelism requested for this plan. For StaticThread this
    // is the number of threads. For ThreadPool and oneTBB it is the
    // maximum number of concurrently active worker tasks.
    std::size_t job_count = 1;

    // Dynamic scheduling grain. Zero means backend default. Static and
    // sequential plans ignore this field.
    std::size_t chunk_size = 0;
};
} // namespace smart
