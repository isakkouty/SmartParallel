#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <memory>
#include <smart/core/config.hpp>
#include <smart/execution/nested_execution_session.hpp>

namespace smart
{
enum class NestedExecutionPolicy
{
    NotNested,
    NativeRuntimeDelegation,
    BudgetLimitedDelegation,
    CooperativeHelping,
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
        case NestedExecutionPolicy::BudgetLimitedDelegation:
            return "budget_limited_delegation";
        case NestedExecutionPolicy::CooperativeHelping:
            return "cooperative_helping";
        case NestedExecutionPolicy::SequentialFallback:
            return "sequential_fallback";
        default:
            return "unknown";
    }
}

struct LoopTelemetryState
{
    std::atomic<std::size_t> nested_call_count{0};
    std::atomic<std::uint64_t> nested_elapsed_ns{0};
};

struct ExecutionContext
{
    std::uint64_t loop_id = 0;
    std::uint64_t parent_loop_id = 0;
    std::size_t depth = 0;
    ExecutionEngineType engine = ExecutionEngineType::Auto;
    bool parallel = false;
    NestedExecutionPolicy nested_policy = NestedExecutionPolicy::NotNested;
    std::size_t concurrency_budget = 1;

    // Stable lineage metadata. Sequential regions preserve these values so a
    // later parallel descendant can rejoin the correct runtime domain and
    // inherited concurrency envelope.
    std::uint64_t root_loop_id = 0;
    std::uint64_t nearest_parallel_ancestor_loop_id = 0;
    std::uint64_t runtime_owner_loop_id = 0;
    ExecutionEngineType root_engine = ExecutionEngineType::Auto;
    ExecutionEngineType nearest_parallel_ancestor_engine = ExecutionEngineType::Auto;
    ExecutionEngineType runtime_owner_engine = ExecutionEngineType::Auto;
    std::size_t inherited_concurrency_budget = 1;

    // Context identity and root-scoped coordination. callsite_hash is stable
    // across sibling invocations of the same templated parallel_for callsite.
    std::size_t callsite_hash = 0;
    std::size_t parent_callsite_hash = 0;
    std::shared_ptr<NestedExecutionSession> nested_session;
    std::shared_ptr<LoopTelemetryState> telemetry;
    bool conservative_nested_learning = false;

    // An ancestor has already selected the only useful parallel frontier.
    // Descendant public parallel_for calls may therefore use the direct
    // sequential path without re-entering the decision system.
    bool frontier_descendants_sealed = false;
    bool collect_nested_telemetry = false;

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

inline void inherit_execution_lineage(ExecutionContext& context,
                                      const ExecutionContext& parent) noexcept
{
    const bool has_parent = parent.depth > 0;
    context.root_loop_id = has_parent
                               ? (parent.root_loop_id == 0 ? parent.loop_id : parent.root_loop_id)
                               : context.loop_id;
    context.root_engine = has_parent
                              ? (parent.root_engine == ExecutionEngineType::Auto
                                     ? parent.engine
                                     : parent.root_engine)
                              : context.engine;

    context.nearest_parallel_ancestor_loop_id =
        has_parent && parent.parallel ? parent.loop_id
                                      : parent.nearest_parallel_ancestor_loop_id;
    context.nearest_parallel_ancestor_engine =
        has_parent && parent.parallel ? parent.engine
                                      : parent.nearest_parallel_ancestor_engine;

    context.inherited_concurrency_budget = has_parent
        ? std::max<std::size_t>(1,
                                parent.parallel ? parent.concurrency_budget
                                                : parent.inherited_concurrency_budget)
        : std::max<std::size_t>(1, context.concurrency_budget);
    context.parent_callsite_hash = has_parent ? parent.callsite_hash : 0;
    if (!context.nested_session && has_parent)
        context.nested_session = parent.nested_session;
    if (has_parent)
    {
        context.conservative_nested_learning = parent.conservative_nested_learning;
        context.frontier_descendants_sealed =
            context.frontier_descendants_sealed || parent.frontier_descendants_sealed;
        context.collect_nested_telemetry =
            context.collect_nested_telemetry || parent.collect_nested_telemetry;
    }

    if (!context.parallel || context.engine == ExecutionEngineType::Auto)
    {
        context.runtime_owner_loop_id = parent.runtime_owner_loop_id;
        context.runtime_owner_engine = parent.runtime_owner_engine;
    }
    else if (has_parent && parent.runtime_owner_loop_id != 0
             && parent.runtime_owner_engine == context.engine)
    {
        context.runtime_owner_loop_id = parent.runtime_owner_loop_id;
        context.runtime_owner_engine = parent.runtime_owner_engine;
    }
    else if (has_parent && parent.parallel && parent.engine == context.engine)
    {
        context.runtime_owner_loop_id = parent.loop_id;
        context.runtime_owner_engine = parent.engine;
    }
    else
    {
        context.runtime_owner_loop_id = context.loop_id;
        context.runtime_owner_engine = context.engine;
    }
}

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
