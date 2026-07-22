#include <smart/execution/thread_pool.hpp>
#include <smart/execution/work_chunk.hpp>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
bool report(const char* label, bool pass, const std::string& details)
{
    std::cout << label << ": " << (pass ? "PASS" : "FAIL") << " [" << details << "]\n";
    return pass;
}
}

int main()
{
    using namespace std::chrono_literals;

    smart::ThreadPool pool(2);
    std::promise<void> blocker_started_promise;
    std::shared_future<void> blocker_started = blocker_started_promise.get_future().share();
    std::promise<void> release_blocker_promise;
    std::shared_future<void> release_blocker = release_blocker_promise.get_future().share();

    pool.submit([&]()
    {
        blocker_started_promise.set_value();
        release_blocker.wait();
    });
    blocker_started.wait();

    constexpr std::size_t iterations = 64;
    std::vector<std::atomic<unsigned>> visits(iterations);
    for (auto& visit : visits)
        visit.store(0, std::memory_order_relaxed);

    std::atomic<bool> unrelated_executed{false};
    std::atomic<bool> continuation_resumed{false};
    struct Observation
    {
        smart::ThreadPool::CooperativeHelpingResult helping;
        bool unrelated_before_resume = false;
    };
    std::promise<Observation> result_promise;
    auto result_future = result_promise.get_future();

    pool.submit([&]()
    {
        // This unrelated job is intentionally queued before the dependency helper.
        // A global helper would execute it while waiting; dependency-local helping must not.
        pool.submit([&]() { unrelated_executed.store(true, std::memory_order_release); });

        smart::SchedulerVisibleWork work(0, iterations, 4);
        const auto helping = pool.execute_visible_work_helping(
            work,
            2,
            [&](const smart::WorkChunk& chunk)
            {
                for (std::size_t i = chunk.begin; i < chunk.end; ++i)
                {
                    visits[i].fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::yield();
                }
            });

        const bool unrelated_before_resume = unrelated_executed.load(std::memory_order_acquire);
        continuation_resumed.store(true, std::memory_order_release);
        result_promise.set_value(Observation{helping, unrelated_before_resume});
    });

    if (result_future.wait_for(5s) != std::future_status::ready)
    {
        std::cerr << "FAIL: dependency-local helping did not make progress.\n";
        release_blocker_promise.set_value();
        pool.wait();
        return 1;
    }

    const Observation observation = result_future.get();
    const auto& helping = observation.helping;
    bool exact_once = true;
    for (const auto& visit : visits)
        exact_once = exact_once && visit.load(std::memory_order_relaxed) == 1;

    const bool local_only = helping.dependency_jobs_executed_by_waiter >= 1
        && helping.unrelated_jobs_executed_by_waiter == 0
        && !observation.unrelated_before_resume;
    const bool priority = helping.continuation_resumed
        && continuation_resumed.load(std::memory_order_acquire)
        && !observation.unrelated_before_resume;

    bool pass = true;
    pass &= report("Waiting worker helps only the awaited dependency", local_only,
                   "dependency_jobs=" + std::to_string(helping.dependency_jobs_executed_by_waiter)
                   + ", unrelated_jobs=" + std::to_string(helping.unrelated_jobs_executed_by_waiter));
    pass &= report("Awaited scheduler-visible work remains exact-once", exact_once,
                   "iterations=" + std::to_string(iterations));
    pass &= report("Completed dependency resumes its continuation first", priority,
                   "continuation=1, unrelated_before_resume="
                   + std::to_string(observation.unrelated_before_resume ? 1 : 0));

    release_blocker_promise.set_value();
    pool.wait();
    const bool unrelated_eventually_runs = unrelated_executed.load(std::memory_order_acquire);
    pass &= report("Unrelated queued work remains available afterward", unrelated_eventually_runs,
                   "eventually_executed=" + std::to_string(unrelated_eventually_runs ? 1 : 0));

    if (!pass)
        return 1;

    std::cout << "PASS: dependency-local helping and continuation priority are correct.\n";
    return 0;
}
