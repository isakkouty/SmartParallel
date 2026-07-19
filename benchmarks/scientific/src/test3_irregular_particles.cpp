#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

struct Particle
{
    double x;
    double y;
    double z;
    double mass;
    std::uint32_t work_units;
};

struct Case
{
    const char* name;
    std::size_t particles;
    int repetitions;
};

struct Result
{
    std::string case_name;
    std::size_t particles = 0;
    std::uint64_t total_work_units = 0;
    std::uint32_t min_work_units = 0;
    std::uint32_t max_work_units = 0;
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
    return (samples.size() % 2 == 0) ? (samples[middle - 1] + samples[middle]) * 0.5
                                     : samples[middle];
}

std::uint32_t mix32(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

std::vector<Particle> make_particles(std::size_t count)
{
    std::vector<Particle> particles(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::uint32_t h = mix32(static_cast<std::uint32_t>(i + 1));
        const double t = static_cast<double>(i) * 0.000173;
        Particle p;
        p.x = std::sin(t * 1.7) * 50.0 + static_cast<double>(h & 255U) * 0.001;
        p.y = std::cos(t * 1.3) * 40.0 + static_cast<double>((h >> 8) & 255U) * 0.001;
        p.z = std::sin(t * 0.7) * std::cos(t * 1.1) * 30.0;
        p.mass = 0.5 + static_cast<double>((h >> 16) & 1023U) / 1024.0;
        // Strongly irregular but deterministic: 16..511 inner iterations.
        p.work_units = 16U + (h % 496U);
        particles[i] = p;
    }
    return particles;
}

double evaluate_particle(const Particle& particle, std::size_t index)
{
    double energy = 0.0;
    const double seed = 0.000031 * static_cast<double>(index + 1);
    for (std::uint32_t k = 0; k < particle.work_units; ++k)
    {
        const double phase = seed + 0.013 * static_cast<double>(k + 1);
        const double dx = particle.x + std::sin(phase) * 3.0;
        const double dy = particle.y + std::cos(phase * 1.37) * 2.0;
        const double dz = particle.z + std::sin(phase * 0.73) * 1.5;
        const double radius2 = dx * dx + dy * dy + dz * dz + 1.0;
        energy +=
            particle.mass * (std::sin(radius2 * 0.001) + std::cos(phase)) / std::sqrt(radius2);
    }
    return energy;
}

void evaluate_sequential(const std::vector<Particle>& particles, std::vector<double>& output)
{
    for (std::size_t i = 0; i < particles.size(); ++i)
        output[i] = evaluate_particle(particles[i], i);
}

void evaluate_smartparallel(const std::vector<Particle>& particles, std::vector<double>& output)
{
    smart::parallel_for(std::size_t{0},
                        particles.size(),
                        [&](std::size_t i)
                        {
                            output[i] = evaluate_particle(particles[i], i);
                        });
}

double maximum_error(const std::vector<double>& lhs, const std::vector<double>& rhs)
{
    double error = 0.0;
    for (std::size_t i = 0; i < lhs.size(); ++i)
        error = std::max(error, std::abs(lhs[i] - rhs[i]));
    return error;
}

double checksum(const std::vector<double>& values)
{
    return std::accumulate(values.begin(), values.end(), 0.0);
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

    output << "case,particles,total_work_units,min_work_units,max_work_units,"
              "sequential_ms,smartparallel_ms,speedup,max_error,sequential_checksum,"
              "smartparallel_checksum,engine,strategy,workers,chunk_size,cache_hit,"
              "sequential_fast_path,correct\n";
    output << std::fixed << std::setprecision(12);
    for (const Result& result : results)
    {
        output << result.case_name << ',' << result.particles << ',' << result.total_work_units
               << ',' << result.min_work_units << ',' << result.max_work_units << ','
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
            argc > 1 ? std::filesystem::path(argv[1])
                     : std::filesystem::path(
                           "validation/output/scientific_test3_irregular_particles.csv");

        smart::global_config().enable_experience = false;
        smart::global_config().enable_utility_model_runtime = false;
        smart::global_config().execution_engine = smart::ExecutionEngineType::Auto;

        const std::vector<Case> cases{
            {"tiny_1000", 1'000, 15},
            {"small_10000", 10'000, 11},
            {"medium_100000", 100'000, 7},
            {"large_500000", 500'000, 5},
        };

        std::vector<Result> results;
        bool all_correct = true;

        std::cout << "==== SmartParallel Scientific Test 3: irregular particles ====\n";
        std::cout << "Kernel: deterministic particle-energy evaluation with 16..511 work units per "
                     "particle\n";
        std::cout << "Purpose: exercise adaptive scheduling under strongly non-uniform iteration "
                     "cost\n\n";

        for (const Case& test_case : cases)
        {
            const std::vector<Particle> particles = make_particles(test_case.particles);
            std::vector<double> sequential(particles.size());
            std::vector<double> smartparallel(particles.size());

            const double sequential_ms =
                median_runtime_ms(test_case.repetitions,
                                  [&]
                                  {
                                      evaluate_sequential(particles, sequential);
                                  });
            const double smartparallel_ms =
                median_runtime_ms(test_case.repetitions,
                                  [&]
                                  {
                                      evaluate_smartparallel(particles, smartparallel);
                                  });

            evaluate_sequential(particles, sequential);
            evaluate_smartparallel(particles, smartparallel);

            std::uint64_t total_work_units = 0;
            std::uint32_t min_work_units = particles.front().work_units;
            std::uint32_t max_work_units = particles.front().work_units;
            for (const Particle& particle : particles)
            {
                total_work_units += particle.work_units;
                min_work_units = std::min(min_work_units, particle.work_units);
                max_work_units = std::max(max_work_units, particle.work_units);
            }

            const double max_error = maximum_error(sequential, smartparallel);
            const double sequential_sum = checksum(sequential);
            const double smart_sum = checksum(smartparallel);
            const bool correct = max_error <= 1e-12;
            all_correct = all_correct && correct;

            const auto& report = smart::global_last_decision_report();
            const auto& diagnostics = smart::global_last_parallel_for_profile_diagnostics();

            Result result;
            result.case_name = test_case.name;
            result.particles = test_case.particles;
            result.total_work_units = total_work_units;
            result.min_work_units = min_work_units;
            result.max_work_units = max_work_units;
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

            std::cout << std::left << std::setw(20) << test_case.name
                      << " particles=" << test_case.particles << " work=" << total_work_units
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
        std::cerr << "Scientific Test 3 failed: " << error.what() << '\n';
        return 1;
    }
}
