#pragma once

#include <smart/core/config.hpp>

namespace smart::vision
{
enum class ExecutionRoute
{
    Auto,
    NativeSequential,
    NativeThreadPool,
    NativeStaticThread,
    NativeOneTbb,
    OpenCV
};

inline const char* execution_route_name(ExecutionRoute route) noexcept
{
    switch (route)
    {
        case ExecutionRoute::Auto:
            return "auto";
        case ExecutionRoute::NativeSequential:
            return "native_sequential";
        case ExecutionRoute::NativeThreadPool:
            return "native_thread_pool";
        case ExecutionRoute::NativeStaticThread:
            return "native_static_thread";
        case ExecutionRoute::NativeOneTbb:
            return "native_one_tbb";
        case ExecutionRoute::OpenCV:
            return "opencv";
    }
    return "unknown";
}

inline bool is_native_route(ExecutionRoute route) noexcept
{
    return route == ExecutionRoute::NativeSequential
        || route == ExecutionRoute::NativeThreadPool
        || route == ExecutionRoute::NativeStaticThread
        || route == ExecutionRoute::NativeOneTbb;
}

inline ExecutionEngineType native_execution_engine(ExecutionRoute route) noexcept
{
    switch (route)
    {
        case ExecutionRoute::NativeThreadPool:
            return ExecutionEngineType::ThreadPool;
        case ExecutionRoute::NativeStaticThread:
            return ExecutionEngineType::StaticThread;
        case ExecutionRoute::NativeOneTbb:
            return ExecutionEngineType::OneTbb;
        default:
            return ExecutionEngineType::Auto;
    }
}
} // namespace smart::vision
