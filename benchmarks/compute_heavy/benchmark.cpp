#include <smart/execution/parallel.hpp>
#include <smart/hardware/hardware.hpp>
#include <smart/execution/backend.hpp>
#include <smart/execution/static_thread_engine.hpp>
#include <smart/core/config.hpp>
#include <smart/core/timing_report.hpp>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

double now_ms()
{
    auto now = std::chrono::high_resolution_clock::now();

    return std::chrono::duration<double, std::milli>(
        now.time_since_epoch()
    ).count();
}

void compute_work(double& value)
{
    for (int i = 0; i < 200; ++i)
    {
        value = std::sqrt(value + 1.0);
        value = std::sin(value) + std::cos(value);
        value = value * 1.000001 + 0.000001;
    }
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
    }

    return "Unknown";
}

void reset_values(std::vector<double>& values)
{
    for (double& value : values)
    {
        value = 1.0;
    }
}

void raw_threaded_for(std::vector<double>& values)
{
    std::size_t total = values.size();
    std::size_t thread_count = smart::hardware_threads();

    if (thread_count == 0)
        thread_count = 1;

    if (thread_count > total)
        thread_count = total;

    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (std::size_t t = 0; t < thread_count; ++t)
    {
        std::size_t begin = (total * t) / thread_count;
        std::size_t end = (total * (t + 1)) / thread_count;

        threads.emplace_back([begin, end, &values]()
        {
            for (std::size_t i = begin; i < end; ++i)
            {
                compute_work(values[i]);
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }
}

struct SmartTiming
{
    double total_ms = 0.0;
    double execution_ms = 0.0;
    double overhead_ms = 0.0;
};

SmartTiming read_smart_timing()
{
    SmartTiming timing;

    double generic_execution = 0.0;
    double specific_execution = 0.0;

    for (const smart::TimingPhase& phase : smart::last_timing_report().phases)
    {
        if (phase.name == "total")
        {
            timing.total_ms += phase.elapsed_ms;
        }
        else if (phase.name == "execution")
        {
            generic_execution += phase.elapsed_ms;
        }
        else if (
            phase.name == "execution_static_chunks" ||
            phase.name == "execution_dynamic_chunks" ||
            phase.name == "execution_sequential")
        {
            specific_execution += phase.elapsed_ms;
        }
        else
        {
            timing.overhead_ms += phase.elapsed_ms;
        }
    }

    if (generic_execution > 0.0)
    {
        timing.execution_ms = generic_execution;
    }
    else
    {
        timing.execution_ms = specific_execution;
    }

    return timing;
}

struct Result
{
    std::size_t size = 0;

    double sequential_ms = 0.0;
    double raw_threaded_ms = 0.0;
    double static_thread_ms = 0.0;
    double onetbb_ms = 0.0;

    double smart_total_ms = 0.0;
    double smart_execution_ms = 0.0;
    double smart_overhead_ms = 0.0;

    const char* best_method = "Sequential";
    double best_ms = 0.0;

    smart::ExecutionPlan smart_plan;
    double difference_ms = 0.0;
    double smart_gap_percent = 0.0;
};

Result run_case(std::size_t count)
{
    constexpr int runs = 10;

    Result result;
    result.size = count;

    std::vector<double> values(count, 1.0);

    double sequential_total = 0.0;
    double raw_threaded_total = 0.0;
    double static_thread_total = 0.0;
    double onetbb_total = 0.0;

    double smart_total = 0.0;
    double smart_execution_total = 0.0;
    double smart_overhead_total = 0.0;

    for (int run = 0; run < runs; ++run)
    {
        double start = 0.0;

        reset_values(values);
        start = now_ms();

        for (double& value : values)
        {
            compute_work(value);
        }

        sequential_total += now_ms() - start;

        reset_values(values);
        start = now_ms();

        raw_threaded_for(values);

        raw_threaded_total += now_ms() - start;

        reset_values(values);
        start = now_ms();

        smart::static_thread_for_each(
            values,
            smart::hardware_threads(),
            compute_work
        );

        static_thread_total += now_ms() - start;

        reset_values(values);
        start = now_ms();

        smart::OneTbbEngine tbb_engine;
        tbb_engine.execute_range(
            values.size(),
            smart::hardware_threads(),
            [&](std::size_t i)
            {
                compute_work(values[i]);
            }
        );

        onetbb_total += now_ms() - start;

        reset_values(values);

        smart::for_each(values, compute_work);

        SmartTiming timing = read_smart_timing();

        smart_total += timing.total_ms;
        smart_execution_total += timing.execution_ms;
        smart_overhead_total += timing.overhead_ms;
    }

    result.sequential_ms = sequential_total / runs;
    result.raw_threaded_ms = raw_threaded_total / runs;
    result.static_thread_ms = static_thread_total / runs;
    result.onetbb_ms = onetbb_total / runs;

    result.smart_total_ms = smart_total / runs;
    result.smart_execution_ms = smart_execution_total / runs;
    result.smart_overhead_ms = smart_overhead_total / runs;

    result.smart_plan = smart::global_last_decision_report().plan;

    result.best_method = "Sequential";
    result.best_ms = result.sequential_ms;

    auto consider_best = [&](const char* method, double value)
    {
        if (value < result.best_ms)
        {
            result.best_method = method;
            result.best_ms = value;
        }
    };

    consider_best("RawThreaded", result.raw_threaded_ms);
    consider_best("StaticThread", result.static_thread_ms);
    consider_best("oneTBB", result.onetbb_ms);
    consider_best("SmartParallel", result.smart_total_ms);

    result.difference_ms = result.smart_total_ms - result.best_ms;

    result.smart_gap_percent =
        result.best_ms > 0.0
            ? (result.difference_ms / result.best_ms) * 100.0
            : 0.0;

    return result;
}

void write_csv_header(std::ofstream& csv)
{
    csv << "size,"
        << "sequential_ms,"
        << "raw_threaded_ms,"
        << "static_thread_ms,"
        << "onetbb_ms,"
        << "smart_total_ms,"
        << "smart_execution_ms,"
        << "smart_overhead_ms,"
        << "best_method,"
        << "best_ms,"
        << "chosen_engine,"
        << "chosen_strategy,"
        << "chosen_jobs,"
        << "chosen_parallel,"
        << "difference_ms,"
        << "smart_gap_percent\n";
}

void write_csv_row(std::ofstream& csv, const Result& result)
{
    csv << result.size << ","
        << result.sequential_ms << ","
        << result.raw_threaded_ms << ","
        << result.static_thread_ms << ","
        << result.onetbb_ms << ","
        << result.smart_total_ms << ","
        << result.smart_execution_ms << ","
        << result.smart_overhead_ms << ","
        << result.best_method << ","
        << result.best_ms << ","
        << smart::engine_name(result.smart_plan.engine) << ","
        << strategy_name(result.smart_plan.strategy) << ","
        << result.smart_plan.job_count << ","
        << result.smart_plan.parallel << ","
        << result.difference_ms << ","
        << result.smart_gap_percent << "\n";
}

void print_result(const Result& result)
{
    std::cout << std::setw(8) << result.size
              << " | best=" << std::setw(13) << result.best_method
              << " (" << std::fixed << std::setprecision(6)
              << result.best_ms << " ms)"
              << " | smart="
              << smart::engine_name(result.smart_plan.engine)
              << "/"
              << strategy_name(result.smart_plan.strategy)
              << " total=" << result.smart_total_ms << " ms"
              << " exec=" << result.smart_execution_ms << " ms"
              << " overhead=" << result.smart_overhead_ms << " ms"
              << " | diff=" << result.difference_ms << " ms"
              << " | gap=" << std::setprecision(2)
              << result.smart_gap_percent
              << "%\n";
}

int main()
{
    smart::global_config().enable_experience = false;
    smart::global_config().enable_timing_diagnostics = true;

    std::ofstream csv(
        "benchmarks\\compute_heavy\\output\\beta_1_0\\results.csv"
    );

    write_csv_header(csv);

    std::cout << "==== Compute Heavy Benchmark ====\n";
    std::cout << "Hardware threads: "
              << smart::hardware_threads()
              << "\n\n";

    const std::vector<std::size_t> sizes = {
        1'000,
        2'500,
        5'000,
        10'000,
        20'000,
        50'000,
        100'000
    };

    for (std::size_t size : sizes)
    {
        Result result = run_case(size);

        write_csv_row(csv, result);
        print_result(result);
    }

    std::cout << "\nResults written to:\n";
    std::cout << "benchmarks\\compute_heavy\\output\\beta_1_0\\results.csv\n";

    return 0;
}