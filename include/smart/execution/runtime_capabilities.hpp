#pragma once

#include <smart/core/config.hpp>

#ifndef SMARTPARALLEL_HAS_TBB
#define SMARTPARALLEL_HAS_TBB 0
#endif

namespace smart
{
struct RuntimeCapabilities
{
    bool supports_native_nesting = false;
    bool uses_shared_workers = false;
    bool supports_concurrency_limit = false;
    bool supports_dynamic_chunks = false;
    bool supports_cooperative_helping = false;
    bool supports_cancellation = false;
    bool supports_scheduler_visible_work = false;
};

inline constexpr bool execution_backend_available(ExecutionEngineType type) noexcept
{
    switch (type)
    {
        case ExecutionEngineType::ThreadPool:
        case ExecutionEngineType::StaticThread:
            return true;
        case ExecutionEngineType::OneTbb:
#if SMARTPARALLEL_HAS_TBB
            return true;
#else
            return false;
#endif
        case ExecutionEngineType::Auto:
        default:
            return false;
    }
}

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
