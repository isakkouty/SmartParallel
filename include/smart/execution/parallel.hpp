#pragma once

#include <cstddef>
#include <stdexcept>

#include <smart/core/timing_scope.hpp>
#include <smart/core/timing_report.hpp>

#include <smart/core/config.hpp>
#include <smart/decision/decision.hpp>
#include <smart/execution/executor.hpp>
#include <smart/hardware/hardware.hpp>
#include <smart/core/statistics.hpp>
#include <smart/workload/fingerprint.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_builder.hpp>
#include <smart/execution/static_thread_engine.hpp>
#include <smart/execution/static_container_engine.hpp>
#include <smart/experience/runtime_experience.hpp>
#include <smart/profiling/isolated_function_profile.hpp>

namespace smart
{

    template <typename Function>
    void parallel_for(std::size_t begin, std::size_t end, Function func)
    {
        std::size_t total = end - begin;

        Workload workload = WorkloadBuilder::index_range(total);

        DecisionEngine engine;

        WorkloadAnalyzer analyzer;
        WorkloadAnalysis analysis = analyzer.analyze(workload);

        ExecutionPlan plan = engine.decide(workload, analysis);
        global_last_decision_report() = engine.last_report();

        ExecutionStats stats = execute_workload(workload, plan, [&](std::size_t i)
        {
            func(begin + i);
        });

        if (global_config().enable_experience)
        {
            record_execution_experience(
                workload,
                nullptr,
                global_last_decision_report(),
                plan,
                stats.elapsed_ms);
        }
    }


    template <typename Container, typename Function>
    void for_each(Container& container, Function func)
    {
        if (global_config().enable_timing_diagnostics)
        {
            clear_timing_report();
        }

        TimingScope total_scope("total");

        Workload workload;

        {
            TimingScope scope("workload_build");
            workload = WorkloadBuilder::container(container);
        }

        WorkloadAnalysis analysis;

        {
            TimingScope scope("workload_analysis");

            WorkloadAnalyzer analyzer;
            analysis = analyzer.analyze(workload);
        }

        smart::FunctionProfile function_profile;

        {
            TimingScope scope("function_profile");

            smart::FunctionProfiler::Config profiler_config;
            profiler_config.min_samples = 4;
            profiler_config.max_samples = 8;
            profiler_config.batch_size = 8;
            profiler_config.measured_parallel_overhead_ms = 1.0;

            function_profile = profile_container_on_copies(
                container,
                func,
                profiler_config);
        }

        ExecutionPlan plan;

        {
            TimingScope scope("decision");

            DecisionEngine engine;
            plan = engine.decide(workload, analysis, &function_profile);
            global_last_decision_report() = engine.last_report();
        }

        if (plan.engine == ExecutionEngineType::StaticThread &&
            plan.strategy == ExecutionStrategy::StaticChunks)
        {
            ExecutionStats stats;

            {
                TimingScope scope("execution_static_chunks");

                Timer timer;

                static_thread_for_each(container, plan.job_count, func);

                stats = ExecutionStats{
                    workload.iterations,
                    timer.elapsed_ms()
                };
            }

            if (global_config().enable_experience)
            {
                TimingScope scope("experience_record");

                record_execution_experience(
                    workload,
                    &function_profile,
                    global_last_decision_report(),
                    plan,
                    stats.elapsed_ms);
            }

            return;
        }

        ExecutionStats stats;

        {
            TimingScope scope("execution");

            stats = execute_workload(workload, plan, [&](std::size_t i)
            {
                func(container[i]);
            });
        }

        if (global_config().enable_experience)
        {
            TimingScope scope("experience_record");

            record_execution_experience(
                workload,
                &function_profile,
                global_last_decision_report(),
                plan,
                stats.elapsed_ms);
        }
    }

    template <typename ContainerA, typename ContainerB, typename Function>
    void for_each_pair(ContainerA& a, ContainerB& b, Function func)
    {
        if (global_config().enable_timing_diagnostics)
        {
            clear_timing_report();
        }

        TimingScope total_scope("total");

        Workload workload;

        {
            TimingScope scope("workload_build");
            workload = WorkloadBuilder::pair_container(a, b);
        }

        if (workload.iterations_saturated)
        {
            throw std::overflow_error(
                "SmartParallel pair workload iteration count overflow");
        }

        std::size_t size_b = static_cast<std::size_t>(b.size());

        WorkloadAnalysis analysis;

        {
            TimingScope scope("workload_analysis");

            WorkloadAnalyzer analyzer;
            analysis = analyzer.analyze(workload);
        }

        smart::FunctionProfile function_profile;

        {
            TimingScope scope("function_profile");

            smart::FunctionProfiler::Config profiler_config;
            profiler_config.min_samples = 4;
            profiler_config.max_samples = 8;
            profiler_config.batch_size = 8;
            profiler_config.measured_parallel_overhead_ms = 1.0;

            function_profile = profile_pair_on_copies(
                a,
                b,
                func,
                profiler_config);
        }

        ExecutionPlan plan;

        {
            TimingScope scope("decision");

            DecisionEngine engine;
            plan = engine.decide(workload, analysis, &function_profile);
            global_last_decision_report() = engine.last_report();
        }

        ExecutionStats stats;

        {
            TimingScope scope("execution");

            stats = execute_workload(workload, plan, [&](std::size_t k)
            {
                std::size_t i = k / size_b;
                std::size_t j = k % size_b;

                func(a[i], b[j]);
            });
        }

        if (global_config().enable_experience)
        {
            TimingScope scope("experience_record");

            record_execution_experience(
                workload,
                &function_profile,
                global_last_decision_report(),
                plan,
                stats.elapsed_ms);
        }
    }
}
