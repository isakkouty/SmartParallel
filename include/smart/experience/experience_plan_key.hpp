#pragma once

#include <cstddef>

#include <smart/decision/execution_plan.hpp>

namespace smart
{
    struct ExperiencePlanKey
    {
        ExecutionEngineType engine = ExecutionEngineType::ThreadPool;
        ExecutionStrategy strategy = ExecutionStrategy::StaticChunks;
        std::size_t job_count = 1;
        std::size_t chunk_size = 0;

        bool operator==(const ExperiencePlanKey& other) const
        {
            return engine == other.engine &&
                   strategy == other.strategy &&
                   job_count == other.job_count &&
                   chunk_size == other.chunk_size;
        }
    };

    struct ExperiencePlanKeyHash
    {
        std::size_t operator()(const ExperiencePlanKey& key) const
        {
            std::size_t h = 0;

            h ^= static_cast<std::size_t>(key.engine) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= static_cast<std::size_t>(key.strategy) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= key.job_count + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= key.chunk_size + 0x9e3779b9 + (h << 6) + (h >> 2);

            return h;
        }
    };
}
