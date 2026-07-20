#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <smart/core/config.hpp>

namespace smart
{
enum class NestedExecutionPolicy
{
    NotNested,
    NativeRuntimeDelegation,
    SequentialFallback
};

inline const char* nested_execution_policy_name(NestedExecutionPolicy policy) noexcept
{
    switch (policy)
    {
        case NestedExecutionPolicy::NotNested:
            return "not_nested";
        case NestedExecutionPolicy::NativeRuntimeDelegation:
            return "native_runtime_delegation";
        case NestedExecutionPolicy::SequentialFallback:
            return "sequential_fallback";
        default:
            return "unknown";
    }
}

struct ExecutionContext
{
    std::uint64_t loop_id = 0;
    std::uint64_t parent_loop_id = 0;
    std::size_t depth = 0;
    ExecutionEngineType engine = ExecutionEngineType::Auto;
    bool parallel = false;
    NestedExecutionPolicy nested_policy = NestedExecutionPolicy::NotNested;

    bool nested() const noexcept
    {
        return depth > 1;
    }
};

namespace detail
{
inline std::atomic<std::uint64_t>& next_loop_id()
{
    static std::atomic<std::uint64_t> value{1};
    return value;
}

inline const ExecutionContext*& active_execution_context()
{
    static thread_local const ExecutionContext* context = nullptr;
    return context;
}

inline ExecutionContext make_execution_context()
{
    const ExecutionContext* parent = active_execution_context();

    ExecutionContext context;
    context.loop_id = next_loop_id().fetch_add(1, std::memory_order_relaxed);
    context.parent_loop_id = parent == nullptr ? 0 : parent->loop_id;
    context.depth = parent == nullptr ? 1 : parent->depth + 1;
    return context;
}

class ExecutionContextScope
{
  public:
    explicit ExecutionContextScope(const ExecutionContext& context) noexcept
        : previous_(active_execution_context())
    {
        active_execution_context() = &context;
    }

    ~ExecutionContextScope()
    {
        active_execution_context() = previous_;
    }

    ExecutionContextScope(const ExecutionContextScope&) = delete;
    ExecutionContextScope& operator=(const ExecutionContextScope&) = delete;

  private:
    const ExecutionContext* previous_ = nullptr;
};
} // namespace detail

inline ExecutionContext current_execution_context() noexcept
{
    const ExecutionContext* context = detail::active_execution_context();
    return context == nullptr ? ExecutionContext{} : *context;
}

inline bool inside_parallel_loop() noexcept
{
    return current_execution_context().depth > 0;
}

inline bool inside_nested_parallel_loop() noexcept
{
    return current_execution_context().nested();
}
} // namespace smart
