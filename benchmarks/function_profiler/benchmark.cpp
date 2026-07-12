#include <smart/profiling/function_profiler.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

struct TestCase
{
    const char* name;
    const char* expected;
    void (*function)(int&);
};

void cheap_increment(int& value)
{
    value++;
}

void cheap_assign(int& value)
{
    value = 42;
}

void cheap_add(int& value)
{
    value += 7;
}

void cheap_multiply(int& value)
{
    value *= 3;
}

void cheap_branch(int& value)
{
    if (value & 1)
        value++;
    else
        value--;
}

void memory_transform(int& value)
{
    value = value * 3 + 7;
}

void memory_branch_transform(int& value)
{
    if (value % 3 == 0)
        value = value * 2 + 1;
    else
        value = value * 5 + 9;
}

void memory_bit_mix(int& value)
{
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
}

void medium_polynomial(int& value)
{
    double x = static_cast<double>(value);

    for (int i = 0; i < 25; ++i)
    {
        x = x * 1.000001 + 0.000001;
        x -= x * 0.0000001;
    }

    value = static_cast<int>(x);
}

void medium_sqrt(int& value)
{
    double x = static_cast<double>(value);

    for (int i = 0; i < 25; ++i)
    {
        x = std::sqrt(x + 1.0);
    }

    value = static_cast<int>(x);
}

void medium_trig(int& value)
{
    double x = static_cast<double>(value);

    for (int i = 0; i < 20; ++i)
    {
        x = std::sin(x) + std::cos(x);
    }

    value = static_cast<int>(x);
}

void medium_hash(int& value)
{
    unsigned int x = static_cast<unsigned int>(value);

    for (int i = 0; i < 64; ++i)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        x *= 2654435761u;
    }

    value = static_cast<int>(x);
}

void heavy_sqrt(int& value)
{
    double x = static_cast<double>(value);

    for (int i = 0; i < 1000; ++i)
    {
        x = std::sqrt(x + 1.0);
        x = x * 1.000001 + 0.000001;
    }

    value = static_cast<int>(x);
}

void heavy_trig(int& value)
{
    double x = static_cast<double>(value);

    for (int i = 0; i < 500; ++i)
    {
        x = std::sin(x) + std::cos(x);
    }

    value = static_cast<int>(x);
}

void heavy_mixed(int& value)
{
    double x = static_cast<double>(value);

    for (int i = 0; i < 300; ++i)
    {
        x = std::sqrt(x + 1.0);
        x = std::sin(x);
        x = std::cos(x);
        x = std::exp(x * 0.0001);
    }

    value = static_cast<int>(x);
}

void heavy_polynomial(int& value)
{
    double x = static_cast<double>(value);

    for (int i = 0; i < 2500; ++i)
    {
        x = x * 1.000001;
        x += 0.000001;
        x -= x * 0.0000001;
    }

    value = static_cast<int>(x);
}

void irregular_small(int& value)
{
    int loops = value % 8;

    for (int i = 0; i < loops; ++i)
    {
        value = value * 3 + 7;
    }
}

void irregular_medium(int& value)
{
    int loops = value % 128;

    double x = static_cast<double>(value);

    for (int i = 0; i < loops; ++i)
    {
        x = std::sqrt(x + 1.0);
    }

    value = static_cast<int>(x);
}

void irregular_heavy(int& value)
{
    int loops = value % 1024;

    double x = static_cast<double>(value);

    for (int i = 0; i < loops; ++i)
    {
        x = std::sqrt(x + 1.0);
        x = std::sin(x) + std::cos(x);
    }

    value = static_cast<int>(x);
}

void branch_heavy(int& value)
{
    int x = value;

    for (int i = 0; i < 512; ++i)
    {
        if ((x + i) & 1)
            x = x * 3 + 7;
        else
            x = x / 2 + 11;
    }

    value = x;
}

struct ProfileResult
{
    const TestCase* test = nullptr;
    std::size_t iterations = 0;
    smart::FunctionProfile profile;
};

void initialize_values(std::vector<int>& values)
{
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        values[i] = static_cast<int>((i * 17 + 3) % 2048);
    }
}

ProfileResult run_case(const TestCase& test, std::size_t iterations)
{
    std::vector<int> values(iterations);
    initialize_values(values);

    smart::FunctionProfiler profiler;

    smart::FunctionProfiler::Config config;
    config.min_samples = 8;
    config.max_samples = 64;
    config.batch_size = 128;
    config.measured_parallel_overhead_ms = 1.0;

    smart::FunctionProfile profile =
        profiler.profile_index_range(
            0,
            values.size(),
            [&](std::size_t i)
            {
                test.function(values[i]);
            },
            config
        );

    ProfileResult result;
    result.test = &test;
    result.iterations = iterations;
    result.profile = profile;

    return result;
}

void write_csv_header(std::ofstream& csv)
{
    csv << "function,"
        << "expected,"
        << "iterations,"
        << "samples,"
        << "avg_ms_per_iteration,"
        << "p95_ms_per_iteration,"
        << "max_ms_per_iteration,"
        << "estimated_total_work_ms,"
        << "estimated_parallel_overhead_ms,"
        << "parallel_worthiness,"
        << "instability_ratio,"
        << "stable\n";
}

void write_csv_row(std::ofstream& csv, const ProfileResult& result)
{
    const smart::FunctionProfile& profile = result.profile;

    csv << result.test->name << ","
        << result.test->expected << ","
        << result.iterations << ","
        << profile.samples << ","
        << profile.avg_ms_per_iteration << ","
        << profile.p95_ms_per_iteration << ","
        << profile.max_ms_per_iteration << ","
        << profile.estimated_total_work_ms << ","
        << profile.estimated_parallel_overhead_ms << ","
        << profile.parallel_worthiness << ","
        << profile.instability_ratio << ","
        << profile.stable << "\n";
}

void print_result(const ProfileResult& result)
{
    const smart::FunctionProfile& profile = result.profile;

    std::cout << std::setw(22) << result.test->name
              << " | expected=" << std::setw(16) << result.test->expected
              << " | avg=" << std::fixed << std::setprecision(6)
              << profile.avg_ms_per_iteration << " ms"
              << " | total=" << profile.estimated_total_work_ms << " ms"
              << " | worthiness=" << profile.parallel_worthiness
              << " | stable=" << (profile.stable ? "yes" : "no")
              << "\n";
}

int main()
{
    std::ofstream csv(
        "benchmarks\\function_profiler\\output\\beta_1_0\\results.csv"
    );

    write_csv_header(csv);

    std::cout << "==== Function Profiler Benchmark ====\n\n";

    const TestCase tests[] =
    {
        {"cheap_increment", "Cheap", cheap_increment},
        {"cheap_assign", "Cheap", cheap_assign},
        {"cheap_add", "Cheap", cheap_add},
        {"cheap_multiply", "Cheap", cheap_multiply},
        {"cheap_branch", "Cheap", cheap_branch},

        {"memory_transform", "Memory-light", memory_transform},
        {"memory_branch_transform", "Memory-light", memory_branch_transform},
        {"memory_bit_mix", "Memory-light", memory_bit_mix},

        {"medium_polynomial", "Medium compute", medium_polynomial},
        {"medium_sqrt", "Medium compute", medium_sqrt},
        {"medium_trig", "Medium compute", medium_trig},
        {"medium_hash", "Medium compute", medium_hash},

        {"heavy_sqrt", "Compute-heavy", heavy_sqrt},
        {"heavy_trig", "Compute-heavy", heavy_trig},
        {"heavy_mixed", "Compute-heavy", heavy_mixed},
        {"heavy_polynomial", "Compute-heavy", heavy_polynomial},

        {"irregular_small", "Irregular-light", irregular_small},
        {"irregular_medium", "Irregular-medium", irregular_medium},
        {"irregular_heavy", "Irregular-heavy", irregular_heavy},
        {"branch_heavy", "Branch-heavy", branch_heavy}
    };

    const std::size_t sizes[] =
    {
        1'000,
        10'000,
        100'000
    };

    for (std::size_t size : sizes)
    {
        std::vector<ProfileResult> results;

        for (const TestCase& test : tests)
        {
            ProfileResult result = run_case(test, size);
            results.push_back(result);
            write_csv_row(csv, result);
        }

        std::sort(
            results.begin(),
            results.end(),
            [](const ProfileResult& a, const ProfileResult& b)
            {
                return a.profile.parallel_worthiness >
                       b.profile.parallel_worthiness;
            }
        );

        std::cout << "Dataset size: " << size << "\n";
        std::cout << "Sorted by parallel worthiness:\n";

        for (const ProfileResult& result : results)
        {
            print_result(result);
        }

        std::cout << "\n";
    }

    std::cout << "Results written to:\n";
    std::cout << "benchmarks\\function_profiler\\output\\beta_1_0\\results.csv\n";

    return 0;
}