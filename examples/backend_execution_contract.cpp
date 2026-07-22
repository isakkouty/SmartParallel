#include <atomic>
#include <cstddef>
#include <iostream>
#include <smart/execution/backend.hpp>

namespace
{
bool verify_backend(smart::ExecutionEngineType type,
                    bool native_nesting,
                    bool cooperative_helping,
                    bool scheduler_visible)
{
    smart::IExecutionBackend& backend = smart::execution_backend(type);
    const smart::RuntimeCapabilities capabilities = backend.capabilities();

    constexpr std::size_t total = 64;
    std::atomic<std::size_t> count{0};
    smart::BackendExecutionRequest request;
    request.total = total;
    request.concurrency_budget = 4;
    request.chunk_size = 2;
    request.function = [&](std::size_t) { count.fetch_add(1, std::memory_order_relaxed); };

    const smart::BackendExecutionResult result = backend.execute(std::move(request));
    const bool passed = backend.type() == type
                        && capabilities.supports_native_nesting == native_nesting
                        && capabilities.supports_cooperative_helping == cooperative_helping
                        && capabilities.supports_scheduler_visible_work == scheduler_visible
                        && result.backend == type && result.effective_budget == 4
                        && result.executed
                        && count.load(std::memory_order_relaxed) == total;

    std::cout << "Backend contract " << backend.name() << ": "
              << (passed ? "PASS" : "FAIL")
              << " [native_nesting=" << capabilities.supports_native_nesting
              << ", cooperative_helping=" << capabilities.supports_cooperative_helping
              << ", scheduler_visible=" << capabilities.supports_scheduler_visible_work
              << "]\n";
    return passed;
}
} // namespace

int main()
{
    const bool thread_pool = verify_backend(
        smart::ExecutionEngineType::ThreadPool, false, true, true);
    const bool one_tbb = verify_backend(
        smart::ExecutionEngineType::OneTbb, true, false, false);
    const bool static_thread = verify_backend(
        smart::ExecutionEngineType::StaticThread, false, false, false);

    const bool compatibility_alias =
        &smart::execution_engine(smart::ExecutionEngineType::ThreadPool)
        == &smart::execution_backend(smart::ExecutionEngineType::ThreadPool);
    std::cout << "Legacy execution_engine compatibility alias: "
              << (compatibility_alias ? "PASS" : "FAIL") << '\n';

    const bool passed = thread_pool && one_tbb && static_thread && compatibility_alias;
    std::cout << (passed ? "PASS: backend-neutral execution contract is correct.\n"
                        : "FAIL: backend-neutral execution contract is incorrect.\n");
    return passed ? 0 : 1;
}
