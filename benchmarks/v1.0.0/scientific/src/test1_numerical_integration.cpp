#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <smart/core/config.hpp>
#include <smart/decision/decision_report.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/execution/parallel.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

constexpr double integration_begin = 0.0;
constexpr double integration_end = 100.0;
constexpr double decay = 0.1;

struct Case
{
    const char* name;
    std::size_t intervals;
    int repetitions;
};

struct Result
{
    std::string case_name;
    std::size_t intervals = 0;
    double sequential_ms = 0.0;
    double smartparallel_ms = 0.0;
    double speedup = 0.0;
    double sequential_result = 0.0;

    double smartparallel_result = 0.0;

    double analytical_result = 0.0;
    double absolute_error = 0.0;
    double discretization_error = 0.0;
    std::string engine;
    std::string strategy;
    std::size_t workers = 1;

    std::size_t chunk_size = 0;

    bool cache_hit = false;
    bool sequential_fast_path = false;
    bool correct = false;
};

template <typename Function>
double median_runtime_ms(int repetitions, Function&& function)
{
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repetitions));

    function(); // Warm-up; it also lets SmartParallel populate its profile cache.
    for (int i = 0; i < repetitions; ++i)
    {
        const auto begin = Clock::now();
        function();
        const auto end = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
    }

    std::sort(samples.begin(), samples.end());
    const std::size_t middle = samples.size() / 2;
    if ((samples.size() % 2) == 0)
        return (samples[middle - 1] + samples[middle]) * 0.5;
    return samples[middle];
}

double integrand(double x)
{
    return std::sin(x) * std::exp(-decay * x);
}

double analytical_integral()
{
    // Integral of exp(-a*x) * sin(x):
    // exp(-a*x) * (-a*sin(x) - cos(x)) / (a*a + 1).
    const auto primitive = [](double x)
    {
        return std::exp(-decay * x) * (-decay * std::sin(x) - std::cos(x)) / (decay * decay + 1.0);
    };
    return primitive(integration_end) - primitive(integration_begin);
}

void integrate_sequential(std::vector<double>& contributions, double step)
{
    for (std::size_t i = 0; i < contributions.size(); ++i)
    {
        const double x = integration_begin + (static_cast<double>(i) + 0.5) * step;
        contributions[i] = integrand(x) * step;
    }
}

void integrate_smartparallel(std::vector<double>& contributions, double step)
{
    smart::parallel_for(std::size_t{0},
                        contributions.size(),
                        [&](std::size_t i)
                        {
                            const double x =
                                integration_begin + (static_cast<double>(i) + 0.5) * step;
                            contributions[i] = integrand(x) * step;
                        });
}

double deterministic_sum(const std::vector<double>& contributions)
{
    return std::accumulate(contributions.begin(), contributions.end(), 0.0);
}

const char* strategy_name(smart::ExecutionStrategy strategy)
{
    switch (strategy)
    {
        case smart::ExecutionStrategy::Sequential:
            return "Sequential";
        case smart::ExecutionStrategy::StaticChunks:
            return "StaticChunks";
        case smart::ExecutionStrategy::DynamicChunks:
            return "DynamicChunks";
        default:
            return "Unknown";
    }
}

void write_csv(const std::filesystem::path& path, const std::vector<Result>& results)
{
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());

    std::ofstream output(path);
    if (!output)
        throw std::runtime_error("Could not open output CSV: " + path.string());

    output << "case,intervals,sequential_ms,smartparallel_ms,speedup,"
              "sequential_result,smartparallel_result,analytical_result,"
              "absolute_error,discretization_error,engine,strategy,workers,chunk_size,"
              "cache_hit,sequential_fast_path,correct\n";
    output << std::fixed << std::setprecision(12);

    for (const Result& result : results)
    {
        output << result.case_name << ',' << result.intervals << ',' << result.sequential_ms << ','
               << result.smartparallel_ms << ',' << result.speedup << ','
               << result.sequential_result << ',' << result.smartparallel_result << ','
               << result.analytical_result << ',' << result.absolute_error << ','
               << result.discretization_error << ',' << result.engine << ',' << result.strategy
               << ',' << result.workers << ',' << result.chunk_size << ','
               << (result.cache_hit ? "true" : "false") << ','
               << (result.sequential_fast_path ? "true" : "false") << ','
               << (result.correct ? "true" : "false") << '\n';
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::filesystem::path output_path =
            argc > 1 ? std::filesystem::path(argv[1])
                     : std::filesystem::path(
                           "validation/output/scientific_test1_numerical_integration.csv");

        smart::global_config().enable_experience = false;
        smart::global_config().enable_utility_model_runtime = false;
        smart::global_config().execution_engine = smart::ExecutionEngineType::Auto;

        const std::vector<Case> cases{
            {"tiny_10000", 10'000, 31},
            {"small_100000", 100'000, 21},
            {"medium_1000000", 1'000'000, 11},
            {"large_10000000", 10'000'000, 7},
        };

        const double analytical = analytical_integral();
        std::vector<Result> results;
        bool all_correct = true;

        std::cout << "==== SmartParallel Scientific Test 1: numerical integration ====\n";
        std::cout << "Integrand: sin(x) * exp(-0.1*x), interval: [0, 100]\n";
        std::cout << "Method: midpoint rule; timings exclude the deterministic final reduction\n\n";

        for (const Case& test_case : cases)
        {
            const double step =
                (integration_end - integration_begin) / static_cast<double>(test_case.intervals);
            std::vector<double> sequential(test_case.intervals);
            std::vector<double> smartparallel(test_case.intervals);

            const double sequential_ms =
                median_runtime_ms(test_case.repetitions,
                                  [&]
                                  {
                                      integrate_sequential(sequential, step);
                                  });
            const double smartparallel_ms =
                median_runtime_ms(test_case.repetitions,
                                  [&]
                                  {
                                      integrate_smartparallel(smartparallel, step);
                                  });

            const double sequential_result = deterministic_sum(sequential);

            const double smartparallel_result = deterministic_sum(smartparallel);
            const double absolute_error = std::abs(sequential_result - smartparallel_result);
            const double discretization_error = std::abs(smartparallel_result - analytical);

            const double tolerance = 1e-12 * std::max(1.0, std::abs(sequential_result));
            const bool correct = absolute_error <= tolerance;
            all_correct = all_correct && correct;

            const auto& report = smart::global_last_decision_report();
            const auto& diagnostics = smart::global_last_parallel_for_profile_diagnostics();

            Result result;
            result.case_name = test_case.name;
            result.intervals = test_case.intervals;
            result.sequential_ms = sequential_ms;
            result.smartparallel_ms = smartparallel_ms;
            result.speedup = sequential_ms / smartparallel_ms;
            result.sequential_result = sequential_result;
            result.smartparallel_result = smartparallel_result;
            result.analytical_result = analytical;
            result.absolute_error = absolute_error;
            result.discretization_error = discretization_error;
            result.engine = smart::engine_name(report.plan.engine);
            result.strategy = strategy_name(report.plan.strategy);
            result.workers = report.plan.job_count;
            result.chunk_size = report.plan.chunk_size;
            result.cache_hit = diagnostics.cache_hit;
            result.sequential_fast_path = diagnostics.sequential_fast_path;
            result.correct = correct;
            results.push_back(result);

            std::cout << std::left << std::setw(18) << test_case.name
                      << " intervals=" << std::setw(10) << test_case.intervals
                      << " plan=" << result.engine << '/' << result.strategy << "/w"
                      << result.workers << "/c" << result.chunk_size
                      << " cache=" << (result.cache_hit ? "hit" : "miss")
                      << " fast_path=" << (result.sequential_fast_path ? "yes" : "no")
                      << " correct=" << (correct ? "yes" : "NO") << '\n';
            std::cout << std::right << std::fixed << std::setprecision(6)
                      << "  sequential=" << sequential_ms << " ms"
                      << " | SmartParallel=" << smartparallel_ms << " ms"
                      << " | speedup=" << result.speedup << "x\n"
                      << std::setprecision(12) << "  result=" << smartparallel_result
                      << " | absolute_error=" << absolute_error
                      << " | analytical_error=" << discretization_error << "\n\n";
        }

        write_csv(output_path, results);
        std::cout << "CSV written to: " << output_path.string() << '\n';
        std::cout << "Correctness: " << (all_correct ? "PASS" : "FAIL") << '\n';
        return all_correct ? 0 : 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Scientific Test 1 failed: " << error.what() << '\n';
        return 1;
    }
}
