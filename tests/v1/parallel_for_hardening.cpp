#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include <smart/execution/parallel.hpp>

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void reset()
{
    auto& config = smart::global_config();
    config.enable_experience = false;
    config.enable_utility_model_runtime = false;
    config.enable_parallel_for_auto_profiling = true;
    config.enable_parallel_for_profile_cache = true;
    config.enable_parallel_for_cached_sequential_fast_path = true;
    config.parallel_for_sequential_fast_path_min_observations = 3;
    config.parallel_for_sequential_fast_path_revalidate_interval = 16;
    smart::global_function_profile_cache().clear();
}

void burn(std::size_t rounds, std::size_t seed)
{
    volatile double value = static_cast<double>(seed + 1);
    for (std::size_t i = 0; i < rounds; ++i)
        value = value * 1.0000001 + static_cast<double>(i & 7U);
    (void)value;
}

void test_concurrent_calls_and_thread_local_reports()
{
    constexpr std::size_t callers = 8;
    constexpr std::size_t iterations = 4096;
    std::atomic<std::size_t> completed{0};
    std::vector<std::future<void>> tasks;
    tasks.reserve(callers);

    for (std::size_t caller = 0; caller < callers; ++caller)
    {
        tasks.push_back(std::async(std::launch::async, [caller, &completed, iterations]
        {
            std::atomic<std::size_t> visits{0};
            smart::parallel_for(0, iterations, [&](std::size_t i)
            {
                visits.fetch_add(1, std::memory_order_relaxed);
                burn(100 + caller * 20, i);
            });
            require(visits.load(std::memory_order_relaxed) == iterations,
                    "concurrent parallel_for lost iterations");
            const auto diagnostics = smart::global_last_parallel_for_profile_diagnostics();
            const auto report = smart::global_last_decision_report();
            require(diagnostics.total_ms >= diagnostics.execution_ms,
                    "thread-local diagnostics were corrupted");
            require(report.plan.job_count >= 1,
                    "thread-local decision report was corrupted");
            completed.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    for (auto& task : tasks) task.get();
    require(completed.load(std::memory_order_relaxed) == callers,
            "not all concurrent callers completed");
}

void test_nested_parallel_for()
{
    constexpr std::size_t outer = 32;
    constexpr std::size_t inner = 64;
    std::vector<std::atomic<unsigned>> visits(outer * inner);
    for (auto& visit : visits) visit.store(0, std::memory_order_relaxed);

    smart::parallel_for(0, outer, [&](std::size_t i)
    {
        smart::parallel_for(0, inner, [&](std::size_t j)
        {
            visits[i * inner + j].fetch_add(1, std::memory_order_relaxed);
        });
    });

    for (const auto& visit : visits)
        require(visit.load(std::memory_order_relaxed) == 1,
                "nested parallel_for skipped or duplicated work");
}

void test_exception_recovery()
{
    bool threw = false;
    try
    {
        smart::parallel_for(0, 10000, [](std::size_t i)
        {
            if (i == 1234) throw std::runtime_error("expected hardening exception");
            burn(20, i);
        });
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    require(threw, "parallel callback exception was swallowed");

    std::atomic<std::size_t> visits{0};
    smart::parallel_for(0, 2048, [&](std::size_t)
    {
        visits.fetch_add(1, std::memory_order_relaxed);
    });
    require(visits.load(std::memory_order_relaxed) == 2048,
            "scheduler did not recover after callback exception");
}

void test_cache_concurrency()
{
    constexpr std::size_t callers = 12;
    constexpr std::size_t repetitions = 8;
    std::vector<std::thread> threads;
    threads.reserve(callers);

    auto callback = [](std::size_t i) { burn(80, i); };
    for (std::size_t caller = 0; caller < callers; ++caller)
    {
        threads.emplace_back([&]
        {
            for (std::size_t repetition = 0; repetition < repetitions; ++repetition)
                smart::parallel_for(0, 8192, callback);
        });
    }
    for (auto& thread : threads) thread.join();

    require(smart::global_function_profile_cache().size() > 0,
            "concurrent profiling failed to populate the cache");
}

void test_large_range_boundary()
{
    constexpr std::size_t begin = 1'000'000;
    constexpr std::size_t count = 100'000;
    std::atomic<std::size_t> visits{0};
    smart::parallel_for(begin, begin + count, [&](std::size_t i)
    {
        require(i >= begin && i < begin + count, "out-of-range index produced");
        visits.fetch_add(1, std::memory_order_relaxed);
    });
    require(visits.load(std::memory_order_relaxed) == count,
            "large-offset range lost iterations");
}
}

int main()
{
    try
    {
        reset();
        test_concurrent_calls_and_thread_local_reports();
        test_nested_parallel_for();
        test_exception_recovery();
        test_cache_concurrency();
        test_large_range_boundary();
        std::cout << "parallel_for hardening: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "parallel_for hardening: FAIL: " << error.what() << "\n";
        return 1;
    }
}
