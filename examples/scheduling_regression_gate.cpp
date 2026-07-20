#include <smart/core/config.hpp>
#include <smart/execution/parallel.hpp>
#include <smart/execution/thread_pool.hpp>
#include <smart/execution/work_chunk.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

std::uint64_t cpu_work(std::size_t seed, std::size_t rounds)
{
    std::uint64_t value = static_cast<std::uint64_t>(seed + 1) * 0x9E3779B185EBCA87ull;
    for (std::size_t i = 0; i < rounds; ++i)
    {
        value ^= value >> 12;
        value ^= value << 25;
        value ^= value >> 27;
        value *= 0x2545F4914F6CDD1Dull;
    }
    return value;
}

template <class Function>
double measure_ms(Function&& function)
{
    const auto begin = Clock::now();
    function();
    const auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

double median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}
} // namespace

int main()
{
    smart::global_config().enable_experience = false;
    smart::global_config().enable_parallel_for_auto_profiling = false;
    smart::global_config().enable_parallel_for_profile_cache = false;

    constexpr std::size_t workers = 4;
    constexpr std::size_t flat_iterations = 4096;
    constexpr std::size_t flat_rounds = 2500;
    constexpr std::size_t nested_iterations = 128;
    constexpr std::size_t nested_rounds = 30000;
    constexpr std::size_t repetitions = 3;

    std::vector<std::uint64_t> flat_results(flat_iterations);
    std::vector<double> flat_parallel_times;
    std::vector<double> flat_visible_times;
    flat_parallel_times.reserve(repetitions);
    flat_visible_times.reserve(repetitions);

    for (std::size_t repetition = 0; repetition < repetitions; ++repetition)
    {
        flat_parallel_times.push_back(measure_ms([&]() {
            smart::parallel_for(0, flat_iterations, [&](std::size_t i) {
                flat_results[i] = cpu_work(i, flat_rounds);
            });
        }));

        smart::ThreadPool pool(workers);
        smart::SchedulerVisibleWork work(0, flat_iterations, 32);
        flat_visible_times.push_back(measure_ms([&]() {
            pool.execute_visible_work(work, workers, [&](const smart::WorkChunk& chunk) {
                for (std::size_t i = chunk.begin; i < chunk.end; ++i)
                    flat_results[i] = cpu_work(i, flat_rounds);
            });
        }));
    }

    const double flat_parallel_ms = median(flat_parallel_times);
    const double flat_visible_ms = median(flat_visible_times);
    const double flat_ratio = flat_parallel_ms > 0.0 ? flat_visible_ms / flat_parallel_ms : 0.0;

    std::vector<std::uint64_t> serial_results(nested_iterations);
    std::vector<std::uint64_t> helping_results(nested_iterations);
    std::vector<double> serial_times;
    std::vector<double> helping_times;
    serial_times.reserve(repetitions);
    helping_times.reserve(repetitions);

    for (std::size_t repetition = 0; repetition < repetitions; ++repetition)
    {
        smart::ThreadPool serial_pool(workers);
        serial_times.push_back(measure_ms([&]() {
            serial_pool.submit([&]() {
                for (std::size_t i = 0; i < nested_iterations; ++i)
                    serial_results[i] = cpu_work(i, nested_rounds);
            });
            serial_pool.wait();
        }));

        smart::ThreadPool helping_pool(workers);
        smart::SchedulerVisibleWork nested_work(0, nested_iterations, 2);
        helping_times.push_back(measure_ms([&]() {
            helping_pool.submit([&]() {
                helping_pool.execute_visible_work_helping(
                    nested_work,
                    workers,
                    [&](const smart::WorkChunk& chunk) {
                        for (std::size_t i = chunk.begin; i < chunk.end; ++i)
                            helping_results[i] = cpu_work(i, nested_rounds);
                    });
            });
            helping_pool.wait();
        }));
    }

    const bool results_match = serial_results == helping_results;
    const double serial_ms = median(serial_times);
    const double helping_ms = median(helping_times);
    const double speedup = helping_ms > 0.0 ? serial_ms / helping_ms : 0.0;
    const bool progress_complete = results_match;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Flat parallel_for median: " << flat_parallel_ms << " ms\n";
    std::cout << "Flat scheduler-visible median: " << flat_visible_ms << " ms\n";
    std::cout << "Flat visible/current ratio: " << flat_ratio << "x\n";
    std::cout << "Nested serial inner median: " << serial_ms << " ms\n";
    std::cout << "Nested helping median: " << helping_ms << " ms\n";
    std::cout << "Nested helping speedup: " << speedup << "x\n";
    std::cout << "Nested benchmark results match: " << (results_match ? "PASS" : "FAIL") << "\n";
    std::cout << "Benchmark/regression gate completed: "
              << (progress_complete ? "PASS" : "FAIL") << "\n";
    std::cout << "NOTE: timings are diagnostic; send the complete output for roadmap decisions.\n";
    return progress_complete ? 0 : 1;
}
