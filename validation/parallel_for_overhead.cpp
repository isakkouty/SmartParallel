#include <algorithm>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <smart/execution/parallel.hpp>
#include <vector>

namespace
{
template <typename F>
double elapsed_ms(F&& f)
{
    const auto start = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

double median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    return values.size() % 2 == 0 ? (values[middle - 1] + values[middle]) * 0.5 : values[middle];
}

struct PhaseSample
{
    double total_ms = 0.0;
    double scheduler_ms = 0.0;
    double cache_lookup_ms = 0.0;
    double analysis_ms = 0.0;
    double profiling_ms = 0.0;
    double decision_ms = 0.0;

    double execution_ms = 0.0;

    bool cache_hit = false;
    bool sequential_fast_path = false;
};

PhaseSample capture_last()
{
    const auto& d = smart::global_last_parallel_for_profile_diagnostics();
    PhaseSample result;
    result.total_ms = d.total_ms;
    result.cache_lookup_ms = d.cache_lookup_ms;
    result.analysis_ms = d.workload_analysis_ms;
    result.profiling_ms = d.profiling_ms;
    result.decision_ms = d.decision_ms;
    result.execution_ms = d.execution_ms;
    result.scheduler_ms =
        std::max(0.0, result.total_ms - result.profiling_ms - result.execution_ms);
    result.cache_hit = d.cache_hit;
    result.sequential_fast_path = d.sequential_fast_path;
    return result;
}

PhaseSample median_sample(const std::vector<PhaseSample>& samples)
{
    auto collect = [&](auto member)
    {
        std::vector<double> values;
        values.reserve(samples.size());
        for (const auto& sample : samples)
            values.push_back(sample.*member);
        return median(std::move(values));
    };

    PhaseSample result;
    result.total_ms = collect(&PhaseSample::total_ms);
    result.scheduler_ms = collect(&PhaseSample::scheduler_ms);
    result.cache_lookup_ms = collect(&PhaseSample::cache_lookup_ms);
    result.analysis_ms = collect(&PhaseSample::analysis_ms);
    result.profiling_ms = collect(&PhaseSample::profiling_ms);
    result.decision_ms = collect(&PhaseSample::decision_ms);
    result.execution_ms = collect(&PhaseSample::execution_ms);
    result.cache_hit = std::count_if(samples.begin(),
                                     samples.end(),
                                     [](const auto& sample)
                                     {
                                         return sample.cache_hit;
                                     })
                       > samples.size() / 2;
    result.sequential_fast_path = std::count_if(samples.begin(),
                                                samples.end(),
                                                [](const auto& sample)
                                                {
                                                    return sample.sequential_fast_path;
                                                })
                                  > samples.size() / 2;
    return result;
}
} // namespace

int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "validation/output/parallel_for_overhead.csv";
    std::ofstream out(path);
    if (!out)
    {
        std::cerr << "Unable to open output CSV: " << path << "\n";
        return 1;
    }

    out << "iterations,sequential_ms,cold_total_ms,cold_scheduler_ms,cold_cache_lookup_ms,"
           "cold_analysis_ms,cold_profiling_ms,cold_decision_ms,cold_execution_ms,"
           "cached_total_ms,cached_scheduler_ms,cached_cache_lookup_ms,cached_analysis_ms,"
           "cached_profiling_ms,cached_decision_ms,cached_execution_ms,cache_hit,"
           "sequential_fast_path\n";

    auto& config = smart::global_config();
    config.enable_experience = false;
    config.enable_parallel_for_profile_cache = true;

    constexpr std::size_t repeats = 9;
    for (std::size_t n : {10u, 100u, 1000u, 10000u, 100000u})
    {
        volatile std::size_t sink = 0;
        auto body = [&](std::size_t i)
        {
            sink += (i & 1u);
        };

        std::vector<double> sequential_samples;
        sequential_samples.reserve(repeats);
        for (std::size_t repeat = 0; repeat < repeats; ++repeat)
            sequential_samples.push_back(elapsed_ms(
                [&]
                {
                    for (std::size_t i = 0; i < n; ++i)
                        body(i);
                }));
        const double sequential = median(std::move(sequential_samples));

        smart::global_function_profile_cache().clear();
        smart::parallel_for(0, n, body);
        const PhaseSample cold = capture_last();

        // Build the independent observations required by the safe sequential
        // path before measuring steady-state cache behavior. Parallel profiles
        // naturally become cache hits during these confirmation calls.
        const std::size_t confirmation_calls =
            std::max<std::size_t>(1, config.parallel_for_sequential_fast_path_min_observations);
        for (std::size_t call = 1; call < confirmation_calls; ++call)
            smart::parallel_for(0, n, body);

        std::vector<PhaseSample> cached_samples;
        cached_samples.reserve(repeats);
        for (std::size_t repeat = 0; repeat < repeats; ++repeat)
        {
            smart::parallel_for(0, n, body);
            cached_samples.push_back(capture_last());
        }
        const PhaseSample cached = median_sample(cached_samples);

        out << n << ',' << std::setprecision(10) << sequential << ',' << cold.total_ms << ','
            << cold.scheduler_ms << ',' << cold.cache_lookup_ms << ',' << cold.analysis_ms << ','
            << cold.profiling_ms << ',' << cold.decision_ms << ',' << cold.execution_ms << ','
            << cached.total_ms << ',' << cached.scheduler_ms << ',' << cached.cache_lookup_ms << ','
            << cached.analysis_ms << ',' << cached.profiling_ms << ',' << cached.decision_ms << ','
            << cached.execution_ms << ',' << (cached.cache_hit ? 1 : 0) << ','
            << (cached.sequential_fast_path ? 1 : 0) << '\n';

        std::cout << n << " seq=" << sequential << " ms"
                  << " cold(total/scheduler/profile/exec)=" << cold.total_ms << '/'
                  << cold.scheduler_ms << '/' << cold.profiling_ms << '/' << cold.execution_ms
                  << " ms"
                  << " cached(total/scheduler/exec)=" << cached.total_ms << '/'
                  << cached.scheduler_ms << '/' << cached.execution_ms << " ms"
                  << " cache_hit=" << (cached.cache_hit ? "yes" : "no")
                  << " fast_path=" << (cached.sequential_fast_path ? "yes" : "no") << '\n';
    }
    return 0;
}
