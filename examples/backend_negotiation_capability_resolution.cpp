#include <cstddef>
#include <functional>
#include <iostream>
#include <smart/decision/execution_plan.hpp>
#include <smart/execution/backend.hpp>
#include <smart/execution/execution_context.hpp>
#include <smart/execution/nested_execution_coordinator.hpp>

namespace
{
class CapabilityBackend final : public smart::IExecutionBackend
{
  public:
    CapabilityBackend(smart::ExecutionEngineType type, smart::RuntimeCapabilities capabilities)
        : type_(type), capabilities_(capabilities)
    {
    }

    smart::ExecutionEngineType type() const noexcept override { return type_; }
    smart::RuntimeCapabilities capabilities() const noexcept override { return capabilities_; }

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
            smart::NestedExecutionMechanism mechanism,
            std::size_t negotiated_budget,
            bool native_resolved,
            bool helping_resolved,
            bool cross_backend)
{
    const auto& negotiation = decision.negotiation;
    const bool passed = negotiation.mechanism == mechanism
                        && negotiation.negotiated_budget == negotiated_budget
                        && negotiation.native_capability_resolved == native_resolved
                        && negotiation.helping_capability_resolved == helping_resolved
                        && negotiation.cross_backend_transition == cross_backend;

    std::cout << label << ": " << (passed ? "PASS" : "FAIL")
              << " [mechanism=" << smart::nested_execution_mechanism_name(negotiation.mechanism)
              << ", requested=" << negotiation.requested_budget
              << ", available=" << negotiation.available_budget
              << ", negotiated=" << negotiation.negotiated_budget
              << ", native=" << negotiation.native_capability_resolved
              << ", helping=" << negotiation.helping_capability_resolved
              << ", cross=" << negotiation.cross_backend_transition
              << ", active_policy=" << smart::nested_execution_policy_name(decision.policy)
              << "]\n";
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
        parallel_plan(smart::ExecutionEngineType::OneTbb, 8),
        smart::execution_backend(smart::ExecutionEngineType::OneTbb),
        smart::execution_backend(smart::ExecutionEngineType::OneTbb));
    const bool native = verify("oneTBB resolves native delegation",
                               one_tbb,
                               smart::NestedExecutionMechanism::NativeDelegation,
                               4,
                               true,
                               false,
                               false);

    const auto thread_pool = coordinator.coordinate(
        parent,
        parallel_plan(smart::ExecutionEngineType::ThreadPool, 4),
        smart::execution_backend(smart::ExecutionEngineType::ThreadPool),
        smart::execution_backend(smart::ExecutionEngineType::ThreadPool));
    const bool helping = verify("ThreadPool resolves cooperative helping",
                                thread_pool,
                                smart::NestedExecutionMechanism::CooperativeHelping,
                                4,
                                false,
                                true,
                                false);

    const auto static_thread = coordinator.coordinate(
        parent,
        parallel_plan(smart::ExecutionEngineType::StaticThread, 4),
        smart::execution_backend(smart::ExecutionEngineType::StaticThread),
        smart::execution_backend(smart::ExecutionEngineType::StaticThread));
    const bool fallback = verify("StaticThread resolves sequential fallback",
                                 static_thread,
                                 smart::NestedExecutionMechanism::SequentialFallback,
                                 1,
                                 false,
                                 false,
                                 false);

    const auto cross = coordinator.coordinate(
        parent,
        parallel_plan(smart::ExecutionEngineType::OneTbb, 4),
        smart::execution_backend(smart::ExecutionEngineType::ThreadPool),
        smart::execution_backend(smart::ExecutionEngineType::OneTbb));
    const bool cross_fallback = verify("Cross-backend transition stays conservative",
                                       cross,
                                       smart::NestedExecutionMechanism::SequentialFallback,
                                       1,
                                       false,
                                       false,
                                       true);

    smart::RuntimeCapabilities incomplete_helping{};
    incomplete_helping.uses_shared_workers = true;
    incomplete_helping.supports_dynamic_chunks = true;
    incomplete_helping.supports_cooperative_helping = true;
    CapabilityBackend incomplete_parent(smart::ExecutionEngineType::StaticThread,
                                        incomplete_helping);
    CapabilityBackend incomplete_child(smart::ExecutionEngineType::StaticThread,
                                       incomplete_helping);
    const auto incomplete = coordinator.coordinate(
        parent,
        parallel_plan(smart::ExecutionEngineType::StaticThread, 4),
        incomplete_parent,
        incomplete_child);
    const bool capability_bundle = verify("Helping requires the complete capability bundle",
                                          incomplete,
                                          smart::NestedExecutionMechanism::SequentialFallback,
                                          1,
                                          false,
                                          false,
                                          false);

    smart::RuntimeCapabilities dual{};
    dual.supports_native_nesting = true;
    dual.uses_shared_workers = true;
    dual.supports_dynamic_chunks = true;
    dual.supports_cooperative_helping = true;
    dual.supports_scheduler_visible_work = true;
    CapabilityBackend dual_parent(smart::ExecutionEngineType::StaticThread, dual);
    CapabilityBackend dual_child(smart::ExecutionEngineType::StaticThread, dual);
    const auto precedence = coordinator.coordinate(
        parent,
        parallel_plan(smart::ExecutionEngineType::StaticThread, 4),
        dual_parent,
        dual_child);
    const bool native_precedence = verify("Native delegation takes precedence over helping",
                                          precedence,
                                          smart::NestedExecutionMechanism::NativeDelegation,
                                          4,
                                          true,
                                          true,
                                          false);

    const bool passed = native && helping && fallback && cross_fallback && capability_bundle
                        && native_precedence;
    std::cout << (passed
                      ? "PASS: backend negotiation and capability resolution are correct.\n"
                      : "FAIL: backend negotiation and capability resolution are incorrect.\n");
    return passed ? 0 : 1;
}
