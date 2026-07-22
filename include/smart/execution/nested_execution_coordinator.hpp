#pragma once

#include <algorithm>
#include <cstddef>
#include <smart/decision/execution_plan.hpp>
#include <smart/execution/backend.hpp>
#include <smart/execution/execution_context.hpp>
#include <smart/execution/nested_budget_partition.hpp>

namespace smart
{
inline ExecutionEngineType resolve_execution_engine_type(ExecutionEngineType type) noexcept
{
    return execution_backend(type).type();
}

struct NestedBackendRelation
{
    ExecutionEngineType parent_backend = ExecutionEngineType::Auto;
    ExecutionEngineType child_backend = ExecutionEngineType::Auto;
    RuntimeCapabilities parent_capabilities{};
    RuntimeCapabilities child_capabilities{};
    bool same_backend = false;
    bool native_nesting_compatible = false;
};


enum class NestedExecutionMechanism
{
    DirectExecution,
    NativeDelegation,
    CooperativeHelping,
    SequentialFallback
};

inline const char* nested_execution_mechanism_name(NestedExecutionMechanism mechanism) noexcept
{
    switch (mechanism)
    {
        case NestedExecutionMechanism::DirectExecution:
            return "direct_execution";
        case NestedExecutionMechanism::NativeDelegation:
            return "native_delegation";
        case NestedExecutionMechanism::CooperativeHelping:
            return "cooperative_helping";
        case NestedExecutionMechanism::SequentialFallback:
            return "sequential_fallback";
        default:
            return "unknown";
    }
}

struct BackendNegotiationResult
{
    NestedExecutionMechanism mechanism = NestedExecutionMechanism::DirectExecution;
    std::size_t requested_budget = 1;
    std::size_t available_budget = 1;
    std::size_t negotiated_budget = 1;
    bool nested_request = false;
    bool same_runtime_domain = false;
    bool native_capability_resolved = false;
    bool helping_capability_resolved = false;
    bool cross_backend_transition = false;
};

struct NestedExecutionConstraints
{
    std::size_t iteration_count = 0;
    std::size_t minimum_iterations_per_worker = 1;
    std::size_t minimum_chunks_per_worker = 1;
    std::size_t target_chunks_per_worker = 1;
    double estimated_total_work_ms = 0.0;
    double estimated_ms_per_iteration = 0.0;
    double minimum_parallel_work_ms = 0.0;
    double target_chunk_ms = 0.0;
};

struct NestedExecutionDecision
{
    NestedExecutionPolicy policy = NestedExecutionPolicy::NotNested;
    ExecutionPlan plan{};
    NestedBackendRelation backend_relation{};
    BackendNegotiationResult negotiation{};
    std::size_t parent_budget = 1;
    std::size_t requested_budget = 1;
    std::size_t allocated_budget = 1;
    std::size_t effective_budget = 1;
    std::size_t sibling_index = 0;
    std::size_t sibling_count = 1;
    bool budget_limited = false;
    bool granularity_limited = false;
    std::size_t granularity_budget = 1;
    bool chunk_size_tuned = false;
    std::size_t original_chunk_size = 0;
    std::size_t effective_chunk_size = 0;

    bool uses_sequential_fallback() const noexcept
    {
        return policy == NestedExecutionPolicy::SequentialFallback;
    }

    bool uses_budget_limited_delegation() const noexcept
    {
        return policy == NestedExecutionPolicy::BudgetLimitedDelegation;
    }

    bool uses_cooperative_helping() const noexcept
    {
        return policy == NestedExecutionPolicy::CooperativeHelping;
    }

    bool partition_exhausted() const noexcept
    {
        return allocated_budget == 0;
    }
};

class NestedExecutionCoordinator
{
  public:
    NestedExecutionDecision enforce_constraints(
        NestedExecutionDecision decision,
        const NestedExecutionConstraints& constraints) const noexcept
    {
        if (!decision.plan.parallel || constraints.iteration_count == 0)
            return decision;

        if (constraints.minimum_parallel_work_ms > 0.0
            && constraints.estimated_total_work_ms > 0.0
            && constraints.estimated_total_work_ms < constraints.minimum_parallel_work_ms)
        {
            decision.policy = NestedExecutionPolicy::SequentialFallback;
            decision.negotiation.mechanism = NestedExecutionMechanism::SequentialFallback;
            decision.negotiation.negotiated_budget = 1;
            decision.plan.parallel = false;
            decision.plan.strategy = ExecutionStrategy::Sequential;
            decision.plan.job_count = 1;
            decision.plan.chunk_size = 0;
            decision.effective_budget = 1;
            decision.granularity_limited = true;
            return decision;
        }

        const std::size_t min_iterations =
            std::max<std::size_t>(1, constraints.minimum_iterations_per_worker);
        const std::size_t work_budget = std::max<std::size_t>(
            1, constraints.iteration_count / min_iterations);

        std::size_t chunk_budget = constraints.iteration_count;
        if (decision.plan.chunk_size > 0)
        {
            const std::size_t chunks =
                (constraints.iteration_count + decision.plan.chunk_size - 1)
                / decision.plan.chunk_size;
            const std::size_t min_chunks =
                std::max<std::size_t>(1, constraints.minimum_chunks_per_worker);
            chunk_budget = std::max<std::size_t>(1, chunks / min_chunks);
        }

        const std::size_t previous_budget =
            std::max<std::size_t>(1, decision.effective_budget);
        decision.granularity_budget =
            std::max<std::size_t>(1, std::min(work_budget, chunk_budget));
        const std::size_t constrained_budget =
            std::min(previous_budget, decision.granularity_budget);
        decision.budget_limited = constrained_budget < decision.requested_budget;
        decision.granularity_limited = constrained_budget < previous_budget;

        if (constrained_budget <= 1)
        {
            decision.policy = NestedExecutionPolicy::SequentialFallback;
            decision.negotiation.mechanism = NestedExecutionMechanism::SequentialFallback;
            decision.negotiation.negotiated_budget = 1;
            decision.plan.parallel = false;
            decision.plan.strategy = ExecutionStrategy::Sequential;
            decision.plan.job_count = 1;
            decision.plan.chunk_size = 0;
            decision.effective_budget = 1;
            return decision;
        }

        decision.effective_budget = constrained_budget;
        decision.negotiation.negotiated_budget = constrained_budget;
        decision.plan.job_count = constrained_budget;

        decision.original_chunk_size = decision.plan.chunk_size;
        decision.effective_chunk_size = decision.plan.chunk_size;
        if (decision.plan.strategy == ExecutionStrategy::DynamicChunks)
        {
            const std::size_t target_chunks = std::max<std::size_t>(
                constrained_budget,
                constrained_budget * std::max<std::size_t>(1, constraints.target_chunks_per_worker));
            const std::size_t maximum_balanced_chunk = std::max<std::size_t>(
                1, (constraints.iteration_count + target_chunks - 1) / target_chunks);
            std::size_t time_target_chunk = maximum_balanced_chunk;
            if (constraints.target_chunk_ms > 0.0
                && constraints.estimated_ms_per_iteration > 0.0)
            {
                time_target_chunk = std::max<std::size_t>(
                    1, static_cast<std::size_t>(
                           constraints.target_chunk_ms / constraints.estimated_ms_per_iteration));
            }
            const std::size_t tuned_chunk = std::max<std::size_t>(
                1, std::min(maximum_balanced_chunk, time_target_chunk));
            if (decision.plan.chunk_size == 0 || decision.plan.chunk_size > tuned_chunk)
            {
                decision.plan.chunk_size = tuned_chunk;
                decision.chunk_size_tuned = true;
            }
            decision.effective_chunk_size = decision.plan.chunk_size;
        }
        return decision;
    }

    NestedExecutionDecision coordinate(const ExecutionContext& parent,
                                       const ExecutionPlan& requested_plan) const
    {
        const NestedBudgetPartition partition = NestedBudgetPartitioner{}.partition(
            parent.concurrency_budget, 1, 0);
        return coordinate(parent, requested_plan, partition);
    }

    NestedExecutionDecision coordinate(const ExecutionContext& parent,
                                       const ExecutionPlan& requested_plan,
                                       const NestedBudgetPartition& partition) const
    {
        const IExecutionBackend& parent_backend = execution_backend(parent.engine);
        const IExecutionBackend& child_backend = execution_backend(requested_plan.engine);
        return coordinate(parent, requested_plan, parent_backend, child_backend, partition);
    }

    NestedExecutionDecision coordinate(const ExecutionContext& parent,
                                       const ExecutionPlan& requested_plan,
                                       const IExecutionBackend& parent_backend,
                                       const IExecutionBackend& child_backend) const
    {
        const NestedBudgetPartition partition = NestedBudgetPartitioner{}.partition(
            parent.concurrency_budget, 1, 0);
        return coordinate(parent, requested_plan, parent_backend, child_backend, partition);
    }

    NestedExecutionDecision coordinate(const ExecutionContext& parent,
                                       const ExecutionPlan& requested_plan,
                                       const IExecutionBackend& parent_backend,
                                       const IExecutionBackend& child_backend,
                                       const NestedBudgetPartition& partition) const
    {
        NestedExecutionDecision decision;
        decision.plan = requested_plan;
        decision.backend_relation = describe_relation(parent_backend, child_backend);
        decision.parent_budget = normalized_budget(parent.concurrency_budget);
        decision.requested_budget = requested_plan.parallel
                                        ? normalized_budget(requested_plan.job_count)
                                        : 1;
        decision.sibling_index = partition.child_index;
        decision.sibling_count = std::max<std::size_t>(1, partition.child_count);
        decision.allocated_budget = std::min(decision.parent_budget, partition.allocated_budget);
        decision.negotiation = negotiate(parent,
                                         requested_plan,
                                         decision.backend_relation,
                                         decision.allocated_budget,
                                         decision.requested_budget);
        decision.policy = select_policy(decision.negotiation);

        if (decision.policy == NestedExecutionPolicy::NativeRuntimeDelegation)
        {
            decision.effective_budget = decision.requested_budget;
            decision.plan.job_count = decision.effective_budget;
        }
        else if (decision.policy == NestedExecutionPolicy::BudgetLimitedDelegation)
        {
            decision.effective_budget = decision.allocated_budget;
            decision.plan.job_count = decision.effective_budget;
        }
        else if (decision.policy == NestedExecutionPolicy::CooperativeHelping)
        {
            decision.effective_budget = decision.negotiation.negotiated_budget;
            decision.plan.parallel = true;
            decision.plan.strategy = ExecutionStrategy::DynamicChunks;
            decision.plan.job_count = decision.effective_budget;
        }
        else if (decision.policy == NestedExecutionPolicy::SequentialFallback)
        {
            decision.plan.parallel = false;
            decision.plan.strategy = ExecutionStrategy::Sequential;
            decision.plan.job_count = 1;
            decision.plan.chunk_size = 0;
            decision.effective_budget = 1;
        }
        else
        {
            decision.allocated_budget = decision.requested_budget;
            decision.effective_budget = decision.plan.parallel
                                            ? decision.requested_budget
                                            : 1;
            decision.plan.job_count = decision.effective_budget;
        }

        return decision;
    }

  private:
    static NestedBackendRelation describe_relation(const IExecutionBackend& parent_backend,
                                                   const IExecutionBackend& child_backend) noexcept
    {
        NestedBackendRelation relation;
        relation.parent_backend = parent_backend.type();
        relation.child_backend = child_backend.type();
        relation.parent_capabilities = parent_backend.capabilities();
        relation.child_capabilities = child_backend.capabilities();
        relation.same_backend = relation.parent_backend == relation.child_backend;
        relation.native_nesting_compatible =
            relation.same_backend && relation.child_capabilities.supports_native_nesting;
        return relation;
    }

    static std::size_t normalized_budget(std::size_t budget) noexcept
    {
        return std::max<std::size_t>(1, budget);
    }

    static BackendNegotiationResult negotiate(const ExecutionContext& parent,
                                                const ExecutionPlan& child_plan,
                                                const NestedBackendRelation& relation,
                                                std::size_t allocated_budget,
                                                std::size_t requested_budget) noexcept
    {
        BackendNegotiationResult result;
        result.requested_budget = requested_budget;
        result.available_budget = allocated_budget;
        result.negotiated_budget = 1;
        result.nested_request = parent.depth > 0 && parent.parallel && child_plan.parallel
                                && child_plan.strategy != ExecutionStrategy::Sequential;
        result.same_runtime_domain = relation.same_backend;
        result.cross_backend_transition = result.nested_request && !relation.same_backend;

        if (!result.nested_request)
        {
            result.mechanism = NestedExecutionMechanism::DirectExecution;
            result.negotiated_budget = requested_budget;
            return result;
        }

        result.native_capability_resolved = relation.same_backend
                                            && relation.child_capabilities.supports_native_nesting;
        result.helping_capability_resolved =
            relation.same_backend
            && relation.parent_capabilities.uses_shared_workers
            && relation.child_capabilities.uses_shared_workers
            && relation.child_capabilities.supports_cooperative_helping
            && relation.child_capabilities.supports_dynamic_chunks
            && relation.child_capabilities.supports_scheduler_visible_work;

        if (allocated_budget <= 1)
        {
            result.mechanism = NestedExecutionMechanism::SequentialFallback;
            return result;
        }

        if (result.native_capability_resolved)
        {
            result.mechanism = NestedExecutionMechanism::NativeDelegation;
            result.negotiated_budget = std::min(requested_budget, allocated_budget);
            return result;
        }

        if (result.helping_capability_resolved)
        {
            result.mechanism = NestedExecutionMechanism::CooperativeHelping;
            result.negotiated_budget = std::min(requested_budget, allocated_budget);
            return result;
        }

        result.mechanism = NestedExecutionMechanism::SequentialFallback;
        return result;
    }

    static NestedExecutionPolicy select_policy(const BackendNegotiationResult& negotiation) noexcept
    {
        if (!negotiation.nested_request)
            return NestedExecutionPolicy::NotNested;

        if (negotiation.mechanism == NestedExecutionMechanism::NativeDelegation)
        {
            return negotiation.requested_budget > negotiation.available_budget
                       ? NestedExecutionPolicy::BudgetLimitedDelegation
                       : NestedExecutionPolicy::NativeRuntimeDelegation;
        }

        if (negotiation.mechanism == NestedExecutionMechanism::CooperativeHelping)
            return NestedExecutionPolicy::CooperativeHelping;

        return NestedExecutionPolicy::SequentialFallback;
    }
};
} // namespace smart
