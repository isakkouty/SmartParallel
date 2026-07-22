#include <smart/execution/backend.hpp>
#include <smart/execution/thread_pool.hpp>
#include <atomic>
#include <cstddef>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
bool report(const char* label, bool passed, const std::string& details) {
    std::cout << label << ": " << (passed ? "PASS" : "FAIL") << " [" << details << "]\n";
    return passed;
}
}

int main() {
    constexpr std::size_t total = 128;
    constexpr std::size_t failure_index = 0;

    smart::ThreadPool pool(4);
    smart::SchedulerVisibleWork work(0, total, 1);
    std::vector<std::atomic<int>> visits(total);
    for (auto& v : visits) v.store(0);
    std::atomic<std::size_t> callbacks{0};
    bool caught = false;
    std::string message;
    try {
        pool.execute_visible_work_helping(work, 4, [&](const smart::WorkChunk& chunk) {
            for (std::size_t i = chunk.begin; i < chunk.end; ++i) {
                visits[i].fetch_add(1);
                callbacks.fetch_add(1);
                if (i == failure_index)
                    throw std::runtime_error("step23-first-failure");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    } catch (const std::runtime_error& e) {
        caught = true;
        message = e.what();
    }

    std::size_t repeated = 0;
    for (auto& v : visits) if (v.load() > 1) ++repeated;
    const bool first_failure_ok = caught && message == "step23-first-failure" && work.cancelled() && work.has_exception();
    const bool cancellation_ok = callbacks.load() < total;
    const bool exact_once_ok = repeated == 0;

    report("First worker failure propagates to the waiting caller", first_failure_ok,
           "caught=" + std::to_string(caught) + ", message=" + message);
    report("Failure requests cooperative cancellation", cancellation_ok,
           "callbacks=" + std::to_string(callbacks.load()) + ", total=" + std::to_string(total));
    report("Cancellation never repeats acquired work", exact_once_ok,
           "repeated=" + std::to_string(repeated));

    smart::BackendExecutionRequest request;
    request.total = 96;
    request.concurrency_budget = 4;
    request.cooperative_helping = true;
    request.chunk_size = 1;
    std::atomic<std::size_t> backend_callbacks{0};
    request.function = [&](std::size_t i) {
        backend_callbacks.fetch_add(1);
        if (i == 0) throw std::logic_error("nested-backend-failure");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    bool backend_caught = false;
    try {
        smart::execution_backend(smart::ExecutionEngineType::ThreadPool).execute(std::move(request));
    } catch (const std::logic_error& e) {
        backend_caught = std::string(e.what()) == "nested-backend-failure";
    }
    const bool backend_ok = backend_caught && backend_callbacks.load() < 96;
    report("Nested ThreadPool backend preserves exception and cancellation", backend_ok,
           "callbacks=" + std::to_string(backend_callbacks.load()));

    const bool passed = first_failure_ok && cancellation_ok && exact_once_ok && backend_ok;
    std::cout << (passed ? "PASS: exception propagation and cooperative cancellation are correct.\n"
                         : "FAIL: exception propagation and cooperative cancellation are incorrect.\n");
    return passed ? 0 : 1;
}
