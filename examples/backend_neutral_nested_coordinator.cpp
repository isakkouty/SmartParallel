#include <cstddef>
#include <functional>
#include <iostream>
#include <smart/decision/execution_plan.hpp>
#include <smart/execution/backend.hpp>
#include <smart/execution/execution_context.hpp>
#include <smart/execution/nested_execution_coordinator.hpp>
#include <utility>

namespace
{
class CapabilityBackend final : public smart::IExecutionBackend
{
  public:
    CapabilityBackend(smart::ExecutionEngineType type, smart::RuntimeCapabilities capabilities)
        : type_(type), capabilities_(capabilities)
    {
    }

    smart::ExecutionEngineType type() const noexcept override
    {
        return type_;
    }

    smart::RuntimeCapabilities capabilities() const noexcept override
    {
        return capabilities_;
    }

    void execute_range(std::size_t total,
                       std::size_t,
                       std::size_t,
                       std::function<void(std::size_t)> function) override
    {
        for (std::size_t i = 0; i < total; ++i)
            function(i);
    }

  private:
    smart::ExecutionEngineType type_;
    smart::RuntimeCapabilities capabilities_;
};

smart::ExecutionPlan parallel_plan(smart::ExecutionEngineType backend, std::size_t jobs)
{
    smart::ExecutionPlan plan;
    plan.parallel = true;
    plan.strategy = smart::ExecutionStrategy::DynamicChunks;
    plan.engine = backend;
    plan.job_count = jobs;
    plan.chunk_size = 1;
    return plan;
}

bool verify(const char* label,
            const smart::NestedExecutionDecision& decision,
            smart::NestedExecutionPolicy policy,
            bool same_backend,
            bool native_compatible,
            std::size_t budget)
{
    const bool passed = decision.policy == policy
                        && decision.backend_relation.same_backend == same_backend
                        && decision.backend_relation.native_nesting_compatible == native_compatible
                        && decision.effective_budget == budget;

    std::cout << label << ": " << (passed ? "PASS" : "FAIL")
              << " [parent=" << smart::runtime_name(decision.backend_relation.parent_backend)
              << ", child=" << smart::runtime_name(decision.backend_relation.child_backend)
              << ", same=" << decision.backend_relation.same_backend
              << ", native=" << decision.backend_relation.native_nesting_compatible
              << ", policy=" << smart::nested_execution_policy_name(decision.policy)
              << ", budget=" << decision.effective_budget << "]\n";
    return passed;
}
} // namespace

int main()
{
    smart::ExecutionContext parent;
    parent.depth = 1;
    parent.parallel = true;
    parent.concurrency_budget = 4;

    smart::NestedExecutionCoordinator coordinator;

    const auto one_tbb = coordinator.coordinate(
        parent,
        parallel_plan(smart::ExecutionEngineType::OneTbb, 4),
        smart::execution_backend(smart::ExecutionEngineType::OneTbb),
        smart::execution_backend(smart::ExecutionEngineType::OneTbb));
    const bool registry_native = verify("Registry oneTBB -> oneTBB",
                                        one_tbb,
                                        smart::NestedExecutionPolicy::NativeRuntimeDelegation,
                                        true,
                                        true,
                                        4);

    const auto cross_backend = coordinator.coordinate(
        parent,
        parallel_plan(smart::ExecutionEngineType::OneTbb, 4),
        smart::execution_backend(smart::ExecutionEngineType::ThreadPool),
        smart::execution_backend(smart::ExecutionEngineType::OneTbb));
    const bool registry_cross = verify("Registry ThreadPool -> oneTBB",
                                       cross_backend,
                                       smart::NestedExecutionPolicy::SequentialFallback,
                                       false,
                                       false,
                                       1);

    smart::RuntimeCapabilities native_capabilities{};
    native_capabilities.supports_native_nesting = true;
    native_capabilities.supports_concurrency_limit = true;
    CapabilityBackend future_parent(smart::ExecutionEngineType::StaticThread, native_capabilities);
    CapabilityBackend future_child(smart::ExecutionEngineType::StaticThread, native_capabilities);

    const auto capability_native = coordinator.coordinate(
        parent,
        parallel_plan(smart::ExecutionEngineType::StaticThread, 8),
        future_parent,
        future_child);
    const bool abstract_native = verify("Capability-driven future backend",
                                        capability_native,
                                        smart::NestedExecutionPolicy::BudgetLimitedDelegation,
                                        true,
                                        true,
                                        4);

    smart::RuntimeCapabilities conservative_capabilities{};
    conservative_capabilities.supports_concurrency_limit = true;
    CapabilityBackend conservative_parent(
        smart::ExecutionEngineType::OneTbb, conservative_capabilities);
    CapabilityBackend conservative_child(
        smart::ExecutionEngineType::OneTbb, conservative_capabilities);

    const auto capability_fallback = coordinator.coordinate(
        parent,
        parallel_plan(smart::ExecutionEngineType::OneTbb, 4),
        conservative_parent,
        conservative_child);
    const bool abstract_fallback = verify("Capabilities override backend name",
                                          capability_fallback,
                                          smart::NestedExecutionPolicy::SequentialFallback,
                                          true,
                                          false,
                                          1);

    const bool passed = registry_native && registry_cross && abstract_native && abstract_fallback;
    std::cout << (passed
                      ? "PASS: backend-neutral nested coordinator is correct.\n"
                      : "FAIL: backend-neutral nested coordinator is incorrect.\n");
    return passed ? 0 : 1;
}
