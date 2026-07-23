#include <smart/core/config.hpp>
#include <smart/decision/decision.hpp>
#include <smart/execution/algorithms.hpp>
#include <smart/execution/backend.hpp>
#include <smart/execution/runtime_capabilities.hpp>
#include <smart/profiling/function_profile_cache.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
constexpr const char* schema_version = "1.4.0";
constexpr std::size_t element_count = 262144;

struct ConfigGuard
{
    smart::Config saved = smart::global_config();
    ~ConfigGuard() { smart::global_config() = saved; }
};

struct BenchmarkCase
{
    std::string name;
    std::size_t iterations = 0;
    std::function<void()> prepare;
    std::function<void()> sequential;
    std::function<void()> parallel;
    std::function<std::uint64_t()> checksum;
};

struct Mode
{
    std::string name;
    bool sequential = false;
    smart::ExecutionEngineType engine = smart::ExecutionEngineType::Auto;
};

struct Sample
{
    double elapsed_ms = 0.0;
    std::uint64_t checksum = 0;
    std::string backend = "sequential";
    bool parallel = false;
    bool backend_authenticated = false;
};

struct Summary
{
    std::string algorithm;
    std::string mode;
    std::size_t repetitions = 0;
    std::size_t iterations = 0;
    double median_ms = 0.0;
    double minimum_ms = 0.0;
    double maximum_ms = 0.0;
    double sequential_median_ms = 0.0;
    double speedup = 0.0;
    std::uint64_t checksum = 0;
    std::uint64_t expected_checksum = 0;
    bool correct = false;
    bool backend_authenticated = false;
    std::string observed_backend = "sequential";
    bool parallel_observed = false;
    std::vector<Sample> samples;
};

std::uint64_t mix(std::uint64_t value, std::size_t rounds = 8)
{
    for (std::size_t round = 0; round < rounds; ++round)
    {
        value += 0x9E3779B97F4A7C15ull + round;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        value ^= value >> 31;
    }
    return value;
}

std::uint64_t checksum_vector(const std::vector<std::uint64_t>& values)
{
    std::uint64_t checksum = 0xCBF29CE484222325ull;
    for (std::size_t index = 0; index < values.size(); ++index)
        checksum ^= mix(values[index] + index, 2);
    return checksum;
}

double median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    return values.size() % 2 == 0
        ? (values[middle - 1] + values[middle]) * 0.5
        : values[middle];
}

void configure_mode(const smart::Config& baseline, const Mode& mode)
{
    smart::global_config() = baseline;
    auto& config = smart::global_config();
    config.enable_experience = false;
    config.enable_experience_ranking = false;
    config.enable_online_exploration = false;
    config.enable_parallel_for_backend_calibration = false;
    config.nested_root_concurrency_budget = 4;
    config.enable_nested_execution_session = true;
    config.enable_parallel_for_profile_cache = true;
    config.enable_parallel_for_auto_profiling = true;
    config.execution_engine = mode.engine;
    if (!mode.sequential && mode.engine != smart::ExecutionEngineType::Auto)
    {
        config.enable_parallel_for_cached_sequential_fast_path = false;
        config.enable_parallel_for_tiny_work_bypass = false;
        config.parallel_for_minimum_predicted_speedup = 0.0;
        config.parallel_for_estimated_overhead_ms = 0.0;
        config.small_workload_iteration_threshold = 0;
        config.cheap_workload_sequential_threshold = 0;
        config.nested_min_iterations_per_worker = 1;
        config.nested_min_parallel_work_ms = 0.0;
        config.nested_plan_hysteresis = 1.0;
    }
    ++config.parallel_for_policy_generation;
    smart::global_function_profile_cache().clear();
}

std::pair<std::string, bool> observed_plan()
{
    const smart::ExecutionPlan plan = smart::global_last_decision_report().plan;
    if (!plan.parallel)
        return {"sequential", false};
    return {smart::runtime_name(smart::resolve_execution_engine_type(plan.engine)), true};
}

bool authenticate_backend(const Mode& mode, const std::string& backend, bool parallel)
{
    if (mode.sequential)
        return !parallel && backend == "sequential";
    if (mode.engine == smart::ExecutionEngineType::Auto)
    {
        if (!parallel)
            return backend == "sequential";
        return backend == "thread_pool" || backend == "static_thread" || backend == "one_tbb";
    }
    return parallel && backend == mode.name;
}

Sample run_sample(const BenchmarkCase& benchmark_case, const Mode& mode)
{
    benchmark_case.prepare();
    const auto start = Clock::now();
    if (mode.sequential)
        benchmark_case.sequential();
    else
        benchmark_case.parallel();
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    auto [backend, parallel] = mode.sequential
        ? std::pair<std::string, bool>{"sequential", false}
        : observed_plan();
    const bool backend_authenticated = authenticate_backend(mode, backend, parallel);
    const std::uint64_t checksum = benchmark_case.checksum();
    return {elapsed_ms, checksum, std::move(backend), parallel, backend_authenticated};
}

Summary benchmark_mode(const BenchmarkCase& benchmark_case,
                       const Mode& mode,
                       std::size_t repetitions,
                       std::uint64_t expected_checksum,
                       double sequential_median_ms)
{
    std::cout << benchmark_case.name << " / " << mode.name << " ... " << std::flush;
    (void)run_sample(benchmark_case, mode);

    Summary summary;
    summary.algorithm = benchmark_case.name;
    summary.mode = mode.name;
    summary.repetitions = repetitions;
    summary.iterations = benchmark_case.iterations;
    summary.expected_checksum = expected_checksum;
    summary.sequential_median_ms = sequential_median_ms;
    summary.samples.reserve(repetitions);

    std::vector<double> elapsed;
    elapsed.reserve(repetitions);
    summary.minimum_ms = std::numeric_limits<double>::max();
    for (std::size_t repetition = 0; repetition < repetitions; ++repetition)
    {
        Sample sample = run_sample(benchmark_case, mode);
        elapsed.push_back(sample.elapsed_ms);
        summary.minimum_ms = std::min(summary.minimum_ms, sample.elapsed_ms);
        summary.maximum_ms = std::max(summary.maximum_ms, sample.elapsed_ms);
        summary.checksum = sample.checksum;
        if (summary.samples.empty())
            summary.observed_backend = sample.backend;
        else if (summary.observed_backend != sample.backend)
            summary.observed_backend = "mixed";
        summary.parallel_observed = summary.parallel_observed || sample.parallel;
        summary.samples.push_back(std::move(sample));
    }

    summary.median_ms = median(elapsed);
    if (mode.sequential)
        summary.sequential_median_ms = summary.median_ms;
    summary.speedup = summary.median_ms > 0.0
        ? summary.sequential_median_ms / summary.median_ms
        : 0.0;
    summary.correct = std::all_of(
        summary.samples.begin(), summary.samples.end(), [&](const Sample& sample) {
            return sample.checksum == expected_checksum;
        });
    summary.backend_authenticated = std::all_of(
        summary.samples.begin(), summary.samples.end(), [](const Sample& sample) {
            return sample.backend_authenticated;
        });

    const bool passed = summary.correct && summary.backend_authenticated;
    std::cout << (passed ? "PASS" : "FAIL")
              << " median=" << std::fixed << std::setprecision(3)
              << summary.median_ms << " ms backend=" << summary.observed_backend << '\n';
    return summary;
}

std::vector<BenchmarkCase> make_cases()
{
    std::vector<BenchmarkCase> cases;

    {
        auto values = std::make_shared<std::vector<std::uint64_t>>(element_count);
        cases.push_back({
            "parallel_for_each", element_count,
            [values] { std::iota(values->begin(), values->end(), std::uint64_t{1}); },
            [values] {
                for (auto& value : *values)
                    value = mix(value);
            },
            [values] {
                smart::parallel_for_each(values->begin(), values->end(), [](std::uint64_t& value) {
                    value = mix(value);
                });
            },
            [values] { return checksum_vector(*values); }});
    }
    {
        auto input = std::make_shared<std::vector<std::uint64_t>>(element_count);
        auto output = std::make_shared<std::vector<std::uint64_t>>(element_count);
        cases.push_back({
            "parallel_transform", element_count,
            [input, output] {
                std::iota(input->begin(), input->end(), std::uint64_t{3});
                std::fill(output->begin(), output->end(), 0);
            },
            [input, output] {
                std::transform(input->begin(), input->end(), output->begin(),
                               [](std::uint64_t value) { return mix(value); });
            },
            [input, output] {
                smart::parallel_transform(input->begin(), input->end(), output->begin(),
                                          [](std::uint64_t value) { return mix(value); });
            },
            [output] { return checksum_vector(*output); }});
    }
    {
        auto left = std::make_shared<std::vector<std::uint64_t>>(element_count);
        auto right = std::make_shared<std::vector<std::uint64_t>>(element_count);
        auto output = std::make_shared<std::vector<std::uint64_t>>(element_count);
        cases.push_back({
            "parallel_transform_binary", element_count,
            [left, right, output] {
                std::iota(left->begin(), left->end(), std::uint64_t{3});
                std::iota(right->begin(), right->end(), std::uint64_t{17});
                std::fill(output->begin(), output->end(), 0);
            },
            [left, right, output] {
                std::transform(left->begin(), left->end(), right->begin(), output->begin(),
                               [](std::uint64_t a, std::uint64_t b) {
                                   return mix(a ^ (b << 1));
                               });
            },
            [left, right, output] {
                smart::parallel_transform(
                    left->begin(), left->end(), right->begin(), output->begin(),
                    [](std::uint64_t a, std::uint64_t b) { return mix(a ^ (b << 1)); });
            },
            [output] { return checksum_vector(*output); }});
    }
    {
        auto input = std::make_shared<std::vector<std::uint64_t>>(element_count * 4);
        auto output = std::make_shared<std::vector<std::uint64_t>>(input->size());
        cases.push_back({
            "parallel_copy", input->size(),
            [input, output] {
                std::iota(input->begin(), input->end(), std::uint64_t{7});
                std::fill(output->begin(), output->end(), 0);
            },
            [input, output] { std::copy(input->begin(), input->end(), output->begin()); },
            [input, output] {
                smart::parallel_copy(input->begin(), input->end(), output->begin());
            },
            [output] { return checksum_vector(*output); }});
    }
    {
        auto values = std::make_shared<std::vector<std::uint64_t>>(element_count * 4);
        cases.push_back({
            "parallel_fill", values->size(),
            [values] { std::fill(values->begin(), values->end(), 0); },
            [values] { std::fill(values->begin(), values->end(), 0x12345678u); },
            [values] {
                smart::parallel_fill(values->begin(), values->end(), std::uint64_t{0x12345678u});
            },
            [values] { return checksum_vector(*values); }});
    }
    {
        auto values = std::make_shared<std::vector<std::uint64_t>>(element_count);
        cases.push_back({
            "parallel_generate", values->size(),
            [values] { std::fill(values->begin(), values->end(), 0); },
            [values] {
                for (std::size_t index = 0; index < values->size(); ++index)
                    (*values)[index] = mix(index + 11);
            },
            [values] {
                smart::parallel_generate(values->begin(), values->end(), [](std::size_t index) {
                    return mix(index + 11);
                });
            },
            [values] { return checksum_vector(*values); }});
    }
    {
        auto values = std::make_shared<std::vector<std::uint64_t>>(element_count);
        auto result = std::make_shared<std::uint64_t>(0);
        cases.push_back({
            "parallel_reduce", values->size(),
            [values, result] {
                for (std::size_t index = 0; index < values->size(); ++index)
                    (*values)[index] = mix(index, 2) & 0xFFFFu;
                *result = 0;
            },
            [values, result] {
                *result = std::accumulate(values->begin(), values->end(), std::uint64_t{17});
            },
            [values, result] {
                *result = smart::parallel_reduce(
                    values->begin(), values->end(), std::uint64_t{17});
            },
            [result] { return *result; }});
    }
    {
        auto values = std::make_shared<std::vector<std::uint64_t>>(element_count);
        auto result = std::make_shared<std::uint64_t>(0);
        cases.push_back({
            "parallel_transform_reduce", values->size(),
            [values, result] {
                std::iota(values->begin(), values->end(), std::uint64_t{1});
                *result = 0;
            },
            [values, result] {
                *result = std::accumulate(
                    values->begin(), values->end(), std::uint64_t{19},
                    [](std::uint64_t sum, std::uint64_t value) {
                        return sum + (mix(value) & 0xFFFFu);
                    });
            },
            [values, result] {
                *result = smart::parallel_transform_reduce(
                    values->begin(), values->end(), std::uint64_t{19}, std::plus<>{},
                    [](std::uint64_t value) { return mix(value) & 0xFFFFu; });
            },
            [result] { return *result; }});
    }
    {
        auto left = std::make_shared<std::vector<std::uint64_t>>(element_count);
        auto right = std::make_shared<std::vector<std::uint64_t>>(element_count);
        auto result = std::make_shared<std::uint64_t>(0);
        cases.push_back({
            "parallel_transform_reduce_binary", element_count,
            [left, right, result] {
                std::iota(left->begin(), left->end(), std::uint64_t{1});
                std::iota(right->begin(), right->end(), std::uint64_t{9});
                *result = 0;
            },
            [left, right, result] {
                std::uint64_t total = 23;
                for (std::size_t index = 0; index < left->size(); ++index)
                    total += mix((*left)[index] ^ ((*right)[index] << 1), 4) & 0xFFFFu;
                *result = total;
            },
            [left, right, result] {
                *result = smart::parallel_transform_reduce(
                    left->begin(), left->end(), right->begin(), std::uint64_t{23},
                    std::plus<>{}, [](std::uint64_t a, std::uint64_t b) {
                        return mix(a ^ (b << 1), 4) & 0xFFFFu;
                    });
            },
            [result] { return *result; }});
    }
    {
        auto values = std::make_shared<std::vector<std::uint64_t>>(element_count);
        auto result = std::make_shared<std::uint64_t>(0);
        cases.push_back({
            "parallel_count", values->size(),
            [values, result] {
                for (std::size_t index = 0; index < values->size(); ++index)
                    (*values)[index] = index % 31;
                *result = 0;
            },
            [values, result] {
                *result = static_cast<std::uint64_t>(
                    std::count(values->begin(), values->end(), 7));
            },
            [values, result] {
                *result = smart::parallel_count(
                    values->begin(), values->end(), std::uint64_t{7});
            },
            [result] { return *result; }});
    }
    {
        auto values = std::make_shared<std::vector<std::uint64_t>>(element_count);
        auto result = std::make_shared<std::uint64_t>(0);
        cases.push_back({
            "parallel_count_if", values->size(),
            [values, result] {
                std::iota(values->begin(), values->end(), std::uint64_t{0});
                *result = 0;
            },
            [values, result] {
                *result = static_cast<std::uint64_t>(std::count_if(
                    values->begin(), values->end(),
                    [](std::uint64_t value) { return (mix(value, 3) & 7u) == 0; }));
            },
            [values, result] {
                *result = smart::parallel_count_if(
                    values->begin(), values->end(),
                    [](std::uint64_t value) { return (mix(value, 3) & 7u) == 0; });
            },
            [result] { return *result; }});
    }
    {
        auto values = std::make_shared<std::vector<std::uint64_t>>(element_count);
        auto result = std::make_shared<std::uint64_t>(0);
        cases.push_back({
            "parallel_any_of", values->size(),
            [values, result] {
                std::iota(values->begin(), values->end(), std::uint64_t{0});
                *result = 0;
            },
            [values, result] {
                *result = std::any_of(values->begin(), values->end(), [](std::uint64_t value) {
                    return value == element_count - 1;
                }) ? 1u : 0u;
            },
            [values, result] {
                *result = smart::parallel_any_of(
                    values->begin(), values->end(), [](std::uint64_t value) {
                        return value == element_count - 1;
                    }) ? 1u : 0u;
            },
            [result] { return *result; }});
    }
    {
        auto values = std::make_shared<std::vector<std::uint64_t>>(element_count);
        auto result = std::make_shared<std::uint64_t>(0);
        cases.push_back({
            "parallel_all_of", values->size(),
            [values, result] {
                std::iota(values->begin(), values->end(), std::uint64_t{1});
                *result = 0;
            },
            [values, result] {
                *result = std::all_of(values->begin(), values->end(), [](std::uint64_t value) {
                    return value != 0;
                }) ? 1u : 0u;
            },
            [values, result] {
                *result = smart::parallel_all_of(
                    values->begin(), values->end(), [](std::uint64_t value) {
                        return value != 0;
                    }) ? 1u : 0u;
            },
            [result] { return *result; }});
    }
    {
        auto values = std::make_shared<std::vector<std::uint64_t>>(element_count);
        auto result = std::make_shared<std::uint64_t>(0);
        cases.push_back({
            "parallel_none_of", values->size(),
            [values, result] {
                std::iota(values->begin(), values->end(), std::uint64_t{1});
                *result = 0;
            },
            [values, result] {
                *result = std::none_of(values->begin(), values->end(), [](std::uint64_t value) {
                    return value == 0;
                }) ? 1u : 0u;
            },
            [values, result] {
                *result = smart::parallel_none_of(
                    values->begin(), values->end(), [](std::uint64_t value) {
                        return value == 0;
                    }) ? 1u : 0u;
            },
            [result] { return *result; }});
    }
    {
        auto values = std::make_shared<std::vector<std::uint64_t>>(element_count);
        auto result = std::make_shared<std::uint64_t>(0);
        cases.push_back({
            "parallel_find", values->size(),
            [values, result] {
                std::iota(values->begin(), values->end(), std::uint64_t{0});
                *result = 0;
            },
            [values, result] {
                *result = static_cast<std::uint64_t>(
                    std::find(values->begin(), values->end(), element_count - 1)
                    - values->begin());
            },
            [values, result] {
                *result = static_cast<std::uint64_t>(
                    smart::parallel_find(values->begin(), values->end(), element_count - 1)
                    - values->begin());
            },
            [result] { return *result; }});
    }
    {
        auto values = std::make_shared<std::vector<std::uint64_t>>(element_count);
        auto result = std::make_shared<std::uint64_t>(0);
        cases.push_back({
            "parallel_find_if", values->size(),
            [values, result] {
                std::iota(values->begin(), values->end(), std::uint64_t{0});
                *result = 0;
            },
            [values, result] {
                *result = static_cast<std::uint64_t>(std::find_if(
                    values->begin(), values->end(), [](std::uint64_t value) {
                        return value == element_count - 1;
                    }) - values->begin());
            },
            [values, result] {
                *result = static_cast<std::uint64_t>(smart::parallel_find_if(
                    values->begin(), values->end(), [](std::uint64_t value) {
                        return value == element_count - 1;
                    }) - values->begin());
            },
            [result] { return *result; }});
    }
    return cases;
}

std::filesystem::path raw_output_path(const std::filesystem::path& summary)
{
    const std::string stem = summary.stem().string();
    return summary.parent_path() / (stem + "_raw.csv");
}

void write_results(const std::filesystem::path& output, const std::vector<Summary>& results)
{
    if (!output.parent_path().empty())
        std::filesystem::create_directories(output.parent_path());
    std::ofstream summary_file(output);
    if (!summary_file)
        throw std::runtime_error("failed to open benchmark summary output");
    summary_file << "schema_version,algorithm,mode,repetitions,iterations,median_ms,min_ms,max_ms,"
                    "sequential_median_ms,speedup,checksum,expected_checksum,correct,"
                    "backend_authenticated,observed_backend,parallel_observed\n";
    summary_file << std::fixed << std::setprecision(6);
    for (const auto& result : results)
    {
        summary_file << schema_version << ',' << result.algorithm << ',' << result.mode << ','
                     << result.repetitions << ',' << result.iterations << ',' << result.median_ms << ','
                     << result.minimum_ms << ',' << result.maximum_ms << ','
                     << result.sequential_median_ms << ',' << result.speedup << ','
                     << result.checksum << ',' << result.expected_checksum << ','
                     << (result.correct ? 1 : 0) << ','
                     << (result.backend_authenticated ? 1 : 0) << ','
                     << result.observed_backend << ','
                     << (result.parallel_observed ? 1 : 0) << '\n';
    }

    std::ofstream raw_file(raw_output_path(output));
    if (!raw_file)
        throw std::runtime_error("failed to open benchmark raw output");
    raw_file << "schema_version,algorithm,mode,repetition,elapsed_ms,checksum,correct,"
                "backend_authenticated,observed_backend,parallel_observed\n";
    raw_file << std::fixed << std::setprecision(6);
    for (const auto& result : results)
    {
        for (std::size_t repetition = 0; repetition < result.samples.size(); ++repetition)
        {
            const auto& sample = result.samples[repetition];
            raw_file << schema_version << ',' << result.algorithm << ',' << result.mode << ','
                     << (repetition + 1) << ',' << sample.elapsed_ms << ',' << sample.checksum << ','
                     << (sample.checksum == result.expected_checksum ? 1 : 0) << ','
                     << (sample.backend_authenticated ? 1 : 0) << ','
                     << sample.backend << ',' << (sample.parallel ? 1 : 0) << '\n';
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    ConfigGuard guard;
    try
    {
        const std::filesystem::path output = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("validation/output/v1.4.0_parallel_algorithms.csv");
        const std::size_t repetitions = argc > 2
            ? static_cast<std::size_t>(std::stoull(argv[2]))
            : 7;
        if (repetitions == 0)
            throw std::invalid_argument("repetitions must be positive");

        const smart::Config baseline = smart::global_config();
        std::vector<Mode> modes{
            {"sequential", true, smart::ExecutionEngineType::Auto},
            {"automatic", false, smart::ExecutionEngineType::Auto},
            {"thread_pool", false, smart::ExecutionEngineType::ThreadPool},
            {"static_thread", false, smart::ExecutionEngineType::StaticThread}};
        if (smart::execution_backend_available(smart::ExecutionEngineType::OneTbb))
            modes.push_back({"one_tbb", false, smart::ExecutionEngineType::OneTbb});

        std::vector<Summary> results;
        bool all_valid = true;
        for (const BenchmarkCase& benchmark_case : make_cases())
        {
            configure_mode(baseline, modes.front());
            benchmark_case.prepare();
            benchmark_case.sequential();
            const std::uint64_t expected = benchmark_case.checksum();

            configure_mode(baseline, modes.front());
            Summary sequential = benchmark_mode(
                benchmark_case, modes.front(), repetitions, expected, 0.0);
            const double sequential_median_ms = sequential.median_ms;
            all_valid = all_valid && sequential.correct && sequential.backend_authenticated;
            results.push_back(std::move(sequential));

            for (std::size_t mode_index = 1; mode_index < modes.size(); ++mode_index)
            {
                configure_mode(baseline, modes[mode_index]);
                Summary result = benchmark_mode(
                    benchmark_case,
                    modes[mode_index],
                    repetitions,
                    expected,
                    sequential_median_ms);
                all_valid = all_valid && result.correct && result.backend_authenticated;
                results.push_back(std::move(result));
            }
        }

        write_results(output, results);
        std::cout << "Summary: " << output << '\n';
        std::cout << "Raw samples: " << raw_output_path(output) << '\n';
        if (!all_valid)
        {
            std::cerr << "SmartParallel v1.4 algorithm benchmark validation: FAIL\n";
            return 1;
        }
        std::cout << "SmartParallel v1.4 algorithm benchmark validation: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SmartParallel v1.4 algorithm benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
