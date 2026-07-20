#pragma once

#include <smart/core/config.hpp>

namespace smart
{
struct RuntimeCapabilities
{
    bool supports_native_nesting = false;
    bool uses_shared_workers = false;
    bool supports_concurrency_limit = false;
    bool supports_dynamic_chunks = false;
};

inline const char* runtime_name(ExecutionEngineType type) noexcept
{
    switch (type)
    {
        case ExecutionEngineType::ThreadPool:
            return "thread_pool";
        case ExecutionEngineType::StaticThread:
            return "static_thread";
        case ExecutionEngineType::OneTbb:
            return "one_tbb";
        case ExecutionEngineType::Auto:
        default:
            return "auto";
    }
}
} // namespace smart
