#include <atomic>
#include <cstddef>
#include <chrono>
#include <exception>
#include <future>
#include <thread>
#include <iostream>
#include <stdexcept>
#include <string>

#include <smart/execution/thread_pool.hpp>
#include <smart/execution/work_chunk.hpp>

namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}


void test_empty_and_repeated_shutdown()
{
    for (std::size_t repetition = 0; repetition < 128; ++repetition)
    {
        smart::ThreadPool pool(1 + repetition % 4);
        require(pool.active_job_count() == 0 && pool.queued_job_count() == 0,
                "new ThreadPool did not start from an empty baseline");
    }

    std::atomic<std::size_t> later_visits{0};
    {
        smart::ThreadPool later(2);
        for (std::size_t i = 0; i < 64; ++i)
            later.submit([&] { later_visits.fetch_add(1, std::memory_order_relaxed); });
        later.wait();
        require(later.active_job_count() == 0 && later.queued_job_count() == 0,
                "independent runtime did not return to baseline after prior shutdowns");
    }
    require(later_visits.load(std::memory_order_relaxed) == 64,
            "later independent runtime failed after repeated shutdown");
}

void test_exception_observation_and_recovery_before_shutdown()
{
    bool observed = false;
    {
        smart::ThreadPool pool(2);
        pool.submit([] { throw std::runtime_error("expected shutdown-path exception"); });
        try
        {
            pool.wait();
        }
        catch (const std::runtime_error& error)
        {
            observed = std::string(error.what()) == "expected shutdown-path exception";
        }
        require(observed, "ThreadPool wait swallowed an exception before shutdown");
        pool.submit([] {});
        pool.wait();
        require(pool.active_job_count() == 0 && pool.queued_job_count() == 0,
                "ThreadPool did not recover after observing an exception");
    }
}

void test_destructor_drains_nested_helper_publication()
{
    std::atomic<std::size_t> visits{0};
    constexpr std::size_t roots = 48;
    constexpr std::size_t iterations = 128;
    {
        smart::ThreadPool pool(4);
        for (std::size_t root = 0; root < roots; ++root)
        {
            pool.submit([&pool, &visits, iterations]
            {
                smart::SchedulerVisibleWork work(0, iterations, 4);
                pool.execute_visible_work_helping(
                    work,
                    4,
                    [&visits](const smart::WorkChunk& chunk)
                    {
                        for (std::size_t index = chunk.begin; index < chunk.end; ++index)
                        {
                            (void)index;
                            visits.fetch_add(1, std::memory_order_relaxed);
                        }
                    });
            });
        }
        // Destruction begins while root jobs are still queued/running. Running
        // workers must be allowed to publish and drain their dependency helpers.
    }
    require(visits.load(std::memory_order_relaxed) == roots * iterations,
            "ThreadPool shutdown abandoned nested helper work");
}

void test_destructor_survives_nested_exception()
{
    std::atomic<std::size_t> completed_roots{0};
    {
        smart::ThreadPool pool(3);
        for (std::size_t root = 0; root < 24; ++root)
        {
            pool.submit([&pool, &completed_roots, root]
            {
                smart::SchedulerVisibleWork work(0, 96, 3);
                try
                {
                    pool.execute_visible_work_helping(
                        work,
                        3,
                        [root](const smart::WorkChunk& chunk)
                        {
                            for (std::size_t index = chunk.begin; index < chunk.end; ++index)
                            {
                                if ((root % 5) == 0 && index == 17)
                                    throw std::runtime_error("expected shutdown exception");
                            }
                        });
                }
                catch (const std::runtime_error&)
                {
                    // Expected for selected roots; the pool must continue
                    // draining other roots and nested helper jobs.
                }
                completed_roots.fetch_add(1, std::memory_order_relaxed);
            });
        }
    }
    require(completed_roots.load(std::memory_order_relaxed) == 24,
            "ThreadPool shutdown stalled after a nested exception");
}
} // namespace

int main()
{
    try
    {
        test_empty_and_repeated_shutdown();
        test_exception_observation_and_recovery_before_shutdown();
        test_destructor_drains_nested_helper_publication();
        test_destructor_survives_nested_exception();
        std::cout << "nested shutdown stress: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "nested shutdown stress: FAIL: " << error.what() << '\n';
        return 1;
    }
}
