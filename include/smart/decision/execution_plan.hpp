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
        std::size_t job_count = 1;
    };  
}
