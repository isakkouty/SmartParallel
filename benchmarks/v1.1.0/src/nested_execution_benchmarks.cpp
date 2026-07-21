#include <smart/execution/parallel.hpp>
#include <smart/execution/executor.hpp>
#include <smart/execution/nested_execution_coordinator.hpp>
#include <smart/workload/workload_builder.hpp>
#include <smart/hardware/hardware.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

smart::ExecutionEngineType benchmark_engine = smart::ExecutionEngineType::ThreadPool;

struct ConfigGuard {
    smart::Config saved = smart::global_config();
    ~ConfigGuard() { smart::global_config() = saved; }
};

struct WorkloadSpec {
    std::string name;
    std::string calculation;
    std::vector<std::size_t> dimensions;
    std::size_t rounds = 0;
    std::uint64_t salt = 0;
};

struct Result {
    std::string suite;
    std::string case_name;
    std::string calculation;
    std::size_t depth = 0;
    std::string configuration;
    std::string parallel_levels;
    std::string dimensions;
    std::size_t total_leaves = 0;
    std::size_t repetitions = 0;
    double median_ms = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    double sequential_median_ms = 0.0;
    double speedup = 0.0;
    std::uint64_t checksum = 0;
    std::uint64_t expected_checksum = 0;
    bool correct = false;
    std::vector<double> raw_samples_ms;
};

std::uint64_t mix(std::uint64_t value, std::size_t rounds)
{
    value += 0x9E3779B97F4A7C15ull;
    for (std::size_t i = 0; i < rounds; ++i) {
        value ^= value >> 30;
        value *= 0xBF58476D1CE4E5B9ull;
        value ^= value >> 27;
        value *= 0x94D049BB133111EBull;
        value ^= value >> 31;
        value += static_cast<std::uint64_t>(i + 1) * 0xD6E8FEB86659FD93ull;
    }
    return value;
}

std::string join_dimensions(const std::vector<std::size_t>& values)
{
    std::string result;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) result += 'x';
        result += std::to_string(values[i]);
    }
    return result;
}

std::string mask_name(std::uint32_t mask, std::size_t depth)
{
    std::string result;
    for (std::size_t level = 0; level < depth; ++level) {
        if ((mask & (1u << level)) == 0) continue;
        if (!result.empty()) result += '+';
        result += 'L';
        result += std::to_string(level + 1);
    }
    return result.empty() ? "none" : result;
}


void coordinated_parallel_for(std::size_t count, const std::function<void(std::size_t)>& body)
{
    smart::ExecutionPlan requested;
    requested.parallel = true;
    requested.engine = benchmark_engine;
    requested.strategy = smart::ExecutionStrategy::DynamicChunks;
    requested.job_count = std::max<std::size_t>(1, smart::current_execution_context().inherited_concurrency_budget);
    requested.chunk_size = 1;

    smart::NestedExecutionCoordinator coordinator;
    auto decision = coordinator.coordinate(smart::current_execution_context(), requested);
    smart::NestedExecutionConstraints constraints;
    constraints.iteration_count = count;
    constraints.minimum_iterations_per_worker = 1;
    constraints.minimum_chunks_per_worker = 1;
    constraints.target_chunks_per_worker = 2;
    decision = coordinator.enforce_constraints(decision, constraints);

    smart::ExecutionContext child = smart::detail::make_execution_context();
    child.engine = decision.plan.parallel
        ? smart::resolve_execution_engine_type(decision.plan.engine)
        : smart::ExecutionEngineType::Auto;
    child.parallel = decision.plan.parallel;
    child.nested_policy = decision.policy;
    child.concurrency_budget = decision.effective_budget;
    smart::inherit_execution_lineage(child, smart::current_execution_context());

    const smart::Workload workload = smart::WorkloadBuilder::index_range(count);
    smart::detail::ExecutionContextScope backend_scope(child);
    smart::execute_workload(workload, decision.plan, [&](std::size_t i) {
        smart::detail::ExecutionContextScope scope(child);
        body(i);
    }, decision.policy, &child);
}

std::size_t leaf_count(const WorkloadSpec& spec)
{
    return std::accumulate(spec.dimensions.begin(), spec.dimensions.end(),
                           std::size_t{1}, std::multiplies<>{});
}

void execute_level(const WorkloadSpec& spec,
                   std::uint32_t parallel_mask,
                   std::size_t level,
                   std::uint64_t path,
                   std::atomic<std::uint64_t>& checksum)
{
    if (level == spec.dimensions.size()) {
        checksum.fetch_xor(mix(path ^ spec.salt, spec.rounds), std::memory_order_relaxed);
        return;
    }

    const auto body = [&](std::size_t index) {
        const std::uint64_t next = path * 1315423911ull
            + static_cast<std::uint64_t>(index + 1) * 2654435761ull
            + static_cast<std::uint64_t>(level + 1) * 2246822519ull;
        execute_level(spec, parallel_mask, level + 1, next, checksum);
    };

    if ((parallel_mask & (1u << level)) != 0)
        coordinated_parallel_for(spec.dimensions[level], body);
    else
        for (std::size_t i = 0; i < spec.dimensions[level]; ++i) body(i);
}

void execute_level_auto(const WorkloadSpec& spec,
                        std::size_t level,
                        std::uint64_t path,
                        std::atomic<std::uint64_t>& checksum)
{
    if (level == spec.dimensions.size()) {
        checksum.fetch_xor(mix(path ^ spec.salt, spec.rounds), std::memory_order_relaxed);
        return;
    }

    smart::parallel_for(0, spec.dimensions[level], [&](std::size_t index) {
        const std::uint64_t next = path * 1315423911ull
            + static_cast<std::uint64_t>(index + 1) * 2654435761ull
            + static_cast<std::uint64_t>(level + 1) * 2246822519ull;
        execute_level_auto(spec, level + 1, next, checksum);
    });
}

std::uint64_t run_once_auto(const WorkloadSpec& spec)
{
    std::atomic<std::uint64_t> checksum{0};
    execute_level_auto(spec, 0, spec.salt, checksum);
    return checksum.load(std::memory_order_relaxed);
}

std::uint64_t run_once_flattened(const WorkloadSpec& spec)
{
    if (spec.dimensions.size() != 4)
        return run_once_auto(spec);
    const std::array<std::size_t, 4> extents{
        spec.dimensions[0], spec.dimensions[1], spec.dimensions[2], spec.dimensions[3]};
    std::atomic<std::uint64_t> checksum{0};
    smart::parallel_for_nd(extents, [&](std::size_t i, std::size_t j, std::size_t k, std::size_t l) {
        std::uint64_t path = spec.salt;
        const std::array<std::size_t, 4> indices{i, j, k, l};
        for (std::size_t level = 0; level < indices.size(); ++level) {
            path = path * 1315423911ull
                + static_cast<std::uint64_t>(indices[level] + 1) * 2654435761ull
                + static_cast<std::uint64_t>(level + 1) * 2246822519ull;
        }
        checksum.fetch_xor(mix(path ^ spec.salt, spec.rounds), std::memory_order_relaxed);
    });
    return checksum.load(std::memory_order_relaxed);
}

std::uint64_t run_once(const WorkloadSpec& spec,
                       std::uint32_t parallel_mask,
                       std::size_t benchmark_budget)
{
    // Benchmark inside a bounded runtime domain. Without this guard the root
    // parallel_for may inherit every logical CPU on a large workstation, and
    // deeply nested cases can enqueue an unnecessarily large helper tree.
    smart::ExecutionContext root;
    root.loop_id = 0xB110;
    root.depth = 1;
    root.engine = benchmark_engine;
    root.parallel = true;
    root.concurrency_budget = std::max<std::size_t>(1, benchmark_budget);
    root.nested_session = std::make_shared<smart::NestedExecutionSession>(
        root.concurrency_budget, root.loop_id);
    smart::inherit_execution_lineage(root, {});
    smart::detail::ExecutionContextScope root_scope(root);

    std::atomic<std::uint64_t> checksum{0};
    execute_level(spec, parallel_mask, 0, spec.salt, checksum);
    return checksum.load(std::memory_order_relaxed);
}

Result benchmark(const std::string& suite,
                 const WorkloadSpec& spec,
                 const std::string& configuration,
                 std::uint32_t mask,
                 std::size_t repetitions,
                 std::uint64_t expected,
                 std::size_t benchmark_budget,
                 std::size_t case_index,
                 std::size_t case_count)
{
    std::cout << "[" << case_index << "/" << case_count << "] "
              << spec.name << " / " << configuration
              << " [levels=" << mask_name(mask, spec.dimensions.size())
              << ", budget=" << benchmark_budget << "]\n" << std::flush;
    std::cout << "  warm-up..." << std::flush;
    (void)run_once(spec, mask, benchmark_budget); // warm-up and profile-cache population
    std::cout << " done\n" << std::flush;
    std::vector<double> samples;
    samples.reserve(repetitions);
    std::uint64_t checksum = 0;
    for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
        std::cout << "  repetition " << (repetition + 1) << "/" << repetitions
                  << "..." << std::flush;
        const auto start = Clock::now();
        checksum = run_once(spec, mask, benchmark_budget);
        const double elapsed =
            std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        samples.push_back(elapsed);
        std::cout << " " << std::fixed << std::setprecision(2) << elapsed
                  << " ms\n" << std::flush;
    }
    const std::vector<double> raw_samples = samples;
    std::sort(samples.begin(), samples.end());

    Result result;
    result.suite = suite;
    result.case_name = spec.name;
    result.calculation = spec.calculation;
    result.depth = spec.dimensions.size();
    result.configuration = configuration;
    result.parallel_levels = mask_name(mask, spec.dimensions.size());
    result.dimensions = join_dimensions(spec.dimensions);
    result.total_leaves = leaf_count(spec);
    result.repetitions = repetitions;
    result.median_ms = samples[samples.size() / 2];
    result.min_ms = samples.front();
    result.max_ms = samples.back();
    result.checksum = checksum;
    result.expected_checksum = expected;
    result.correct = checksum == expected;
    result.raw_samples_ms = raw_samples;
    return result;
}

template <typename Runner>
Result benchmark_runner(const std::string& suite,
                        const WorkloadSpec& spec,
                        const std::string& configuration,
                        const std::string& parallel_levels,
                        std::size_t repetitions,
                        std::uint64_t expected,
                        std::size_t benchmark_budget,
                        std::size_t case_index,
                        std::size_t case_count,
                        Runner runner)
{
    std::cout << "[" << case_index << "/" << case_count << "] "
              << spec.name << " / " << configuration
              << " [levels=" << parallel_levels
              << ", budget=" << benchmark_budget << "]\n" << std::flush;
    std::cout << "  warm-up (cold learning + stable plan)..." << std::flush;
    (void)runner();
    (void)runner();
    std::cout << " done\n" << std::flush;

    std::vector<double> samples;
    samples.reserve(repetitions);
    std::uint64_t checksum = 0;
    for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
        std::cout << "  repetition " << (repetition + 1) << "/" << repetitions
                  << "..." << std::flush;
        const auto start = Clock::now();
        checksum = runner();
        const double elapsed =
            std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        samples.push_back(elapsed);
        std::cout << " " << std::fixed << std::setprecision(2) << elapsed
                  << " ms\n" << std::flush;
    }
    const std::vector<double> raw_samples = samples;
    std::sort(samples.begin(), samples.end());

    Result result;
    result.suite = suite;
    result.case_name = spec.name;
    result.calculation = spec.calculation;
    result.depth = spec.dimensions.size();
    result.configuration = configuration;
    result.parallel_levels = parallel_levels;
    result.dimensions = join_dimensions(spec.dimensions);
    result.total_leaves = leaf_count(spec);
    result.repetitions = repetitions;
    result.median_ms = samples[samples.size() / 2];
    result.min_ms = samples.front();
    result.max_ms = samples.back();
    result.checksum = checksum;
    result.expected_checksum = expected;
    result.correct = checksum == expected;
    result.raw_samples_ms = raw_samples;
    return result;
}

void write_raw_csv(const std::filesystem::path& path, const std::vector<Result>& results)
{
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Unable to open raw CSV output: " + path.string());
    out << "suite,case,configuration,repetition,elapsed_ms\n";
    out << std::fixed << std::setprecision(6);
    for (const auto& result : results)
        for (std::size_t i = 0; i < result.raw_samples_ms.size(); ++i)
            out << result.suite << ',' << result.case_name << ',' << result.configuration << ','
                << (i + 1) << ',' << result.raw_samples_ms[i] << '\n';
}

void write_csv(const std::filesystem::path& path,
               const std::vector<Result>& results,
               std::size_t hardware_threads,
               smart::ExecutionEngineType engine)
{
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Unable to open CSV output: " + path.string());
    out << "suite,case,calculation,depth,configuration,parallel_levels,dimensions,total_leaves,"
           "repetitions,median_ms,min_ms,max_ms,sequential_median_ms,speedup_vs_sequential,"
           "checksum,expected_checksum,correct,engine,hardware_threads\n";
    out << std::fixed << std::setprecision(6);
    for (const auto& r : results) {
        out << r.suite << ',' << r.case_name << ',' << r.calculation << ',' << r.depth << ','
            << r.configuration << ',' << r.parallel_levels << ',' << r.dimensions << ','
            << r.total_leaves << ',' << r.repetitions << ',' << r.median_ms << ',' << r.min_ms
            << ',' << r.max_ms << ',' << r.sequential_median_ms << ',' << r.speedup << ','
            << r.checksum << ',' << r.expected_checksum << ',' << (r.correct ? 1 : 0)
            << ',' << smart::runtime_name(engine) << ',' << hardware_threads << '\n';
    }
}

void attach_baselines(std::vector<Result>& results)
{
    for (auto& result : results) {
        const auto baseline = std::find_if(results.begin(), results.end(), [&](const Result& other) {
            return other.suite == result.suite && other.case_name == result.case_name
                && other.configuration == "all_sequential";
        });
        if (baseline == results.end()) throw std::runtime_error("Missing sequential baseline");
        result.sequential_median_ms = baseline->median_ms;
        result.speedup = result.median_ms > 0.0 ? baseline->median_ms / result.median_ms : 0.0;
    }
}
} // namespace

int main(int argc, char** argv)
{
    ConfigGuard guard;

    bool trace_enabled = false;
    for (int argument = 3; argument < argc; ++argument)
    {
        const std::string mode = argv[argument];
        if (mode == "trace")
            trace_enabled = true;
        else if (mode == "tbb" || mode == "one_tbb")
            benchmark_engine = smart::ExecutionEngineType::OneTbb;
        else if (mode == "thread_pool")
            benchmark_engine = smart::ExecutionEngineType::ThreadPool;
        else
            throw std::invalid_argument("Unknown benchmark mode: " + mode);
    }

    if (benchmark_engine == smart::ExecutionEngineType::OneTbb
        && !smart::execution_backend_available(benchmark_engine))
    {
        std::cerr << "ERROR: oneTBB benchmark requested, but this build has no oneTBB backend.\n";
        return 2;
    }

    auto& config = smart::global_config();
    config.execution_engine = benchmark_engine;
    config.enable_experience = false;
    config.enable_parallel_for_profile_cache = true;
    config.enable_parallel_for_cached_sequential_fast_path = false;
    config.enable_parallel_for_auto_profiling = true;
    config.parallel_for_profile_min_samples = 4;
    config.parallel_for_profile_max_samples = 12;
    config.parallel_for_profile_min_signal_ms = 0.005;
    config.parallel_for_estimated_overhead_ms = 0.001;
    config.parallel_for_minimum_predicted_speedup = 1.01;
    config.enable_nested_execution_session = true;
    config.enable_nested_parallel_frontier = true;
    config.enable_nested_frontier_deferral = true;
    config.enable_nested_frontier_promotion = true;
    config.enable_nested_online_telemetry = true;
    config.enable_nested_execution_trace = trace_enabled;
    config.nested_min_parallel_work_ms = 0.05;
    config.nested_target_chunk_ms = 0.05;

    const std::filesystem::path output = argc > 1
        ? std::filesystem::path(argv[1])
        : std::filesystem::path("validation/output/v1.1.0_nested_execution_benchmarks.csv");
    const std::size_t repetitions = argc > 2 ? std::max(3, std::stoi(argv[2])) : 7;
    const std::size_t hardware_threads = std::max(1u, std::thread::hardware_concurrency());
    const std::size_t benchmark_budget = std::min<std::size_t>(4, hardware_threads);
    config.nested_root_concurrency_budget = benchmark_budget;
    smart::clear_nested_execution_trace();
    smart::reset_one_tbb_execution_count();
    constexpr std::size_t benchmark_case_count = 17;
    std::size_t benchmark_case_index = 0;

    std::cout << "Benchmark backend: " << smart::runtime_name(benchmark_engine) << "\n"
              << "Benchmark runtime domain: " << benchmark_budget
              << " workers (hardware threads=" << hardware_threads << ")\n"
              << std::flush;

    const std::vector<WorkloadSpec> depth_cases{
        {"depth2_grid_transform", "integer_mix", {32, 32}, 700, 0x2100},
        {"depth3_volume_kernel", "nonlinear_hash", {12, 12, 12}, 520, 0x3100},
        {"depth4_tensor_kernel", "mixed_integer_kernel", {2, 3, 4, 128}, 360, 0x4100},
    };

    std::vector<Result> results;
    for (const auto& spec : depth_cases) {
        const auto expected = run_once(spec, 0, benchmark_budget);
        results.push_back(benchmark("depth_scaling", spec, "all_sequential", 0, repetitions, expected,
                                    benchmark_budget, ++benchmark_case_index, benchmark_case_count));
        const std::uint32_t all_nested = (1u << spec.dimensions.size()) - 1u;
        results.push_back(benchmark("depth_scaling", spec, "forced_all_levels", all_nested,
                                    repetitions, expected, benchmark_budget,
                                    ++benchmark_case_index, benchmark_case_count));
        results.push_back(benchmark_runner("depth_scaling", spec, "automatic_all_levels", "auto",
                                           repetitions, expected, benchmark_budget,
                                           ++benchmark_case_index, benchmark_case_count,
                                           [&] { return run_once_auto(spec); }));
    }

    const WorkloadSpec configuration_case{
        "depth4_configuration_matrix", "mixed_integer_kernel", {2, 3, 4, 192}, 360, 0x5100};
    const auto expected = run_once(configuration_case, 0, benchmark_budget);
    const std::vector<std::pair<std::string, std::uint32_t>> configurations{
        {"all_sequential", 0u},
        {"only_level_1_parallel", 1u << 0},
        {"only_level_2_parallel", 1u << 1},
        {"only_level_3_parallel", 1u << 2},
        {"only_level_4_parallel", 1u << 3},
        {"forced_all_levels", 0xFu},
    };
    for (const auto& [name, mask] : configurations)
        results.push_back(benchmark("configuration_matrix", configuration_case, name, mask,
                                    repetitions, expected, benchmark_budget,
                                    ++benchmark_case_index, benchmark_case_count));
    results.push_back(benchmark_runner("configuration_matrix", configuration_case,
                                       "automatic_all_levels", "auto", repetitions, expected,
                                       benchmark_budget, ++benchmark_case_index, benchmark_case_count,
                                       [&] { return run_once_auto(configuration_case); }));
    results.push_back(benchmark_runner("configuration_matrix", configuration_case,
                                       "flattened_nd", "flattened", repetitions, expected,
                                       benchmark_budget, ++benchmark_case_index, benchmark_case_count,
                                       [&] { return run_once_flattened(configuration_case); }));

    attach_baselines(results);
    write_csv(output, results, hardware_threads, benchmark_engine);
    const auto raw_output = output.parent_path() / (output.stem().string() + "_raw.csv");
    const auto trace_output = output.parent_path() / (output.stem().string() + "_trace.csv");
    write_raw_csv(raw_output, results);
    smart::write_nested_execution_trace_csv(
        trace_output.string(), smart::nested_execution_trace_snapshot());

    const std::uint64_t tbb_executions = smart::one_tbb_execution_count();
    const bool backend_verified = benchmark_engine != smart::ExecutionEngineType::OneTbb
        || tbb_executions > 0;

    std::cout << std::fixed << std::setprecision(2);
    bool passed = backend_verified;
    std::cout << "Backend verification: requested=" << smart::runtime_name(benchmark_engine)
              << ", one_tbb_execute_calls=" << tbb_executions
              << ", status=" << (backend_verified ? "PASS" : "FAIL") << '\n';
    std::cout << "Nested depth benchmarks:\n";
    for (const auto& r : results) {
        passed = passed && r.correct && std::isfinite(r.median_ms) && r.median_ms > 0.0;
        std::cout << "  " << r.case_name << " / " << r.configuration
                  << ": " << (r.correct ? "PASS" : "FAIL")
                  << " [median=" << r.median_ms << " ms, speedup=" << r.speedup
                  << "x, levels=" << r.parallel_levels << "]\n";
    }
    std::cout << "CSV written: " << output.string() << '\n';
    std::cout << "Raw samples: " << raw_output.string() << '\n';
    std::cout << "Decision trace: " << trace_output.string() << '\n';
    std::cout << (passed
        ? "PASS: v1.1.0 nested execution benchmarks completed correctly.\n"
        : "FAIL: v1.1.0 nested execution benchmarks detected an error.\n");
    return passed ? 0 : 1;
}
