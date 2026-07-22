#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <smart/core/config.hpp>
#include <smart/decision/decision_report.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/execution/parallel.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
constexpr double alpha = 0.20;

struct Case
{
    const char* name;
    std::size_t width;
    std::size_t height;
    int time_steps;
    int repetitions;
};

struct Result
{
    std::string case_name;
    std::size_t width = 0;
    std::size_t height = 0;
    int time_steps = 0;
    double sequential_ms = 0.0;
    double smartparallel_ms = 0.0;

    double speedup = 0.0;
    double max_error = 0.0;
    double sequential_checksum = 0.0;
    double smartparallel_checksum = 0.0;
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

    function();
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

std::vector<double> make_initial_field(std::size_t width, std::size_t height)
{
    std::vector<double> field(width * height, 0.0);
    const double cx = static_cast<double>(width - 1) * 0.5;
    const double cy = static_cast<double>(height - 1) * 0.5;
    const double sigma = static_cast<double>(std::min(width, height)) * 0.12;
    const double denominator = 2.0 * sigma * sigma;

    for (std::size_t y = 1; y + 1 < height; ++y)
    {
        for (std::size_t x = 1; x + 1 < width; ++x)
        {
            const double dx = static_cast<double>(x) - cx;
            const double dy = static_cast<double>(y) - cy;
            const double hotspot = 100.0 * std::exp(-(dx * dx + dy * dy) / denominator);
            const double ripple = 2.0 * std::sin(0.017 * static_cast<double>(x))
                                  * std::cos(0.013 * static_cast<double>(y));
            field[y * width + x] = hotspot + ripple;
        }
    }
    return field;
}

void update_rows_sequential(const std::vector<double>& input,
                            std::vector<double>& output,
                            std::size_t width,
                            std::size_t height)
{
    for (std::size_t y = 1; y + 1 < height; ++y)
    {
        const std::size_t row = y * width;
        for (std::size_t x = 1; x + 1 < width; ++x)
        {
            const std::size_t i = row + x;
            const double center = input[i];
            output[i] = center
                        + alpha
                              * (input[i - 1] + input[i + 1] + input[i - width] + input[i + width]
                                 - 4.0 * center);
        }
    }
}

void update_rows_smartparallel(const std::vector<double>& input,
                               std::vector<double>& output,
                               std::size_t width,
                               std::size_t height)
{
    smart::parallel_for(std::size_t{1},
                        height - 1,
                        [&](std::size_t y)
                        {
                            const std::size_t row = y * width;
                            for (std::size_t x = 1; x + 1 < width; ++x)
                            {
                                const std::size_t i = row + x;
                                const double center = input[i];
                                output[i] = center
                                            + alpha
                                                  * (input[i - 1] + input[i + 1] + input[i - width]
                                                     + input[i + width] - 4.0 * center);
                            }
                        });
}

void simulate_sequential(const std::vector<double>& initial,
                         std::vector<double>& state,
                         std::vector<double>& scratch,
                         std::size_t width,
                         std::size_t height,
                         int time_steps)
{
    state = initial;
    scratch = initial;
    for (int step = 0; step < time_steps; ++step)
    {
        update_rows_sequential(state, scratch, width, height);
        state.swap(scratch);
    }
}

void simulate_smartparallel(const std::vector<double>& initial,
                            std::vector<double>& state,
                            std::vector<double>& scratch,
                            std::size_t width,
                            std::size_t height,
                            int time_steps)
{
    state = initial;
    scratch = initial;
    for (int step = 0; step < time_steps; ++step)
    {
        update_rows_smartparallel(state, scratch, width, height);
        state.swap(scratch);
    }
}

double checksum(const std::vector<double>& field)
{
    double sum = 0.0;
    for (double value : field)
        sum += value;
    return sum;
}

double maximum_error(const std::vector<double>& lhs, const std::vector<double>& rhs)
{
    double error = 0.0;
    for (std::size_t i = 0; i < lhs.size(); ++i)
        error = std::max(error, std::abs(lhs[i] - rhs[i]));
    return error;
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

    output << "case,width,height,cells,time_steps,sequential_ms,smartparallel_ms,speedup,"
              "max_error,sequential_checksum,smartparallel_checksum,engine,strategy,workers,"
              "chunk_size,cache_hit,sequential_fast_path,correct\n";
    output << std::fixed << std::setprecision(12);
    for (const Result& result : results)
    {
        output << result.case_name << ',' << result.width << ',' << result.height << ','
               << result.width * result.height << ',' << result.time_steps << ','
               << result.sequential_ms << ',' << result.smartparallel_ms << ',' << result.speedup
               << ',' << result.max_error << ',' << result.sequential_checksum << ','
               << result.smartparallel_checksum << ',' << result.engine << ',' << result.strategy
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
            argc > 1
                ? std::filesystem::path(argv[1])
                : std::filesystem::path("validation/output/scientific_test2_heat_diffusion.csv");

        smart::global_config().enable_experience = false;
        smart::global_config().enable_utility_model_runtime = false;
        smart::global_config().execution_engine = smart::ExecutionEngineType::Auto;

        const std::vector<Case> cases{
            {"tiny_128x128", 128, 128, 30, 21},
            {"small_512x512", 512, 512, 40, 11},
            {"medium_1024x1024", 1024, 1024, 40, 7},
            {"large_2048x2048", 2048, 2048, 30, 5},
        };

        std::vector<Result> results;
        bool all_correct = true;

        std::cout << "==== SmartParallel Scientific Test 2: 2D heat diffusion ====\n";
        std::cout << "Kernel: five-point explicit stencil, alpha=" << alpha << '\n';
        std::cout << "Boundary condition: fixed zero-temperature border\n\n";

        for (const Case& test_case : cases)
        {
            const std::vector<double> initial =
                make_initial_field(test_case.width, test_case.height);
            std::vector<double> sequential_state(initial.size());
            std::vector<double> sequential_scratch(initial.size());
            std::vector<double> smart_state(initial.size());
            std::vector<double> smart_scratch(initial.size());

            const double sequential_ms =
                median_runtime_ms(test_case.repetitions,
                                  [&]
                                  {
                                      simulate_sequential(initial,
                                                          sequential_state,
                                                          sequential_scratch,
                                                          test_case.width,
                                                          test_case.height,
                                                          test_case.time_steps);
                                  });
            const double smartparallel_ms =
                median_runtime_ms(test_case.repetitions,
                                  [&]
                                  {
                                      simulate_smartparallel(initial,
                                                             smart_state,
                                                             smart_scratch,
                                                             test_case.width,
                                                             test_case.height,
                                                             test_case.time_steps);
                                  });

            // Recompute once after timing so both result buffers unambiguously contain the final
            // field.
            simulate_sequential(initial,
                                sequential_state,
                                sequential_scratch,
                                test_case.width,
                                test_case.height,
                                test_case.time_steps);
            simulate_smartparallel(initial,
                                   smart_state,
                                   smart_scratch,
                                   test_case.width,
                                   test_case.height,
                                   test_case.time_steps);

            const double max_error = maximum_error(sequential_state, smart_state);
            const double sequential_sum = checksum(sequential_state);
            const double smart_sum = checksum(smart_state);
            const double tolerance = 1e-12;
            const bool correct = max_error <= tolerance;
            all_correct = all_correct && correct;

            const auto& report = smart::global_last_decision_report();
            const auto& diagnostics = smart::global_last_parallel_for_profile_diagnostics();

            Result result;
            result.case_name = test_case.name;
            result.width = test_case.width;
            result.height = test_case.height;
            result.time_steps = test_case.time_steps;
            result.sequential_ms = sequential_ms;
            result.smartparallel_ms = smartparallel_ms;
            result.speedup = sequential_ms / smartparallel_ms;
            result.max_error = max_error;
            result.sequential_checksum = sequential_sum;
            result.smartparallel_checksum = smart_sum;
            result.engine = smart::engine_name(report.plan.engine);
            result.strategy = strategy_name(report.plan.strategy);
            result.workers = report.plan.job_count;
            result.chunk_size = report.plan.chunk_size;
            result.cache_hit = diagnostics.cache_hit;
            result.sequential_fast_path = diagnostics.sequential_fast_path;
            result.correct = correct;
            results.push_back(result);

            std::cout << std::left << std::setw(20) << test_case.name << " grid=" << test_case.width
                      << 'x' << test_case.height << " steps=" << test_case.time_steps
                      << " plan=" << result.engine << '/' << result.strategy << "/w"
                      << result.workers << "/c" << result.chunk_size
                      << " cache=" << (result.cache_hit ? "hit" : "miss")
                      << " fast_path=" << (result.sequential_fast_path ? "yes" : "no")
                      << " correct=" << (correct ? "yes" : "NO") << '\n';
            std::cout << std::right << std::fixed << std::setprecision(6)
                      << "  sequential=" << sequential_ms << " ms"
                      << " | SmartParallel=" << smartparallel_ms << " ms"
                      << " | speedup=" << result.speedup << "x\n"
                      << std::setprecision(12) << "  max_error=" << max_error
                      << " | checksum=" << smart_sum << "\n\n";
        }

        write_csv(output_path, results);
        std::cout << "CSV written to: " << output_path.string() << '\n';
        std::cout << "Correctness: " << (all_correct ? "PASS" : "FAIL") << '\n';
        return all_correct ? 0 : 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Scientific Test 2 failed: " << error.what() << '\n';
        return 1;
    }
}
