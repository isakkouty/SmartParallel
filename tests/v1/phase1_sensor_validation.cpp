#include <smart/model/performance_model.hpp>
#include <smart/profiling/function_profiler.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_builder.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <vector>

namespace
{
    void burn(std::size_t iterations)
    {
        volatile std::size_t sink = 1;

        for (std::size_t i = 0; i < iterations; ++i)
        {
            sink = sink * 1664525u + 1013904223u + i;
        }
    }

    smart::FunctionProfiler::Config validation_config()
    {
        smart::FunctionProfiler::Config config;

        config.min_samples = 6;
        config.max_samples = 24;
        config.batch_size = 1;
        config.max_batch_size = 64;
        config.max_callback_invocations = 2048;
        config.local_sample_count = 3;
        config.target_batch_duration_ms = 0.01;
        config.max_profile_time_ms = 100.0;
        config.relative_error_target = 0.01;

        return config;
    }
}

int main()
{
    smart::FunctionProfiler profiler;

    const smart::FunctionProfiler::Config config =
        validation_config();

    const smart::FunctionProfile cheap =
        profiler.profile_index_range(
            0,
            512,
            [](std::size_t)
            {
                burn(50);
            },
            config
        );

    const smart::FunctionProfile heavy =
        profiler.profile_index_range(
            0,
            512,
            [](std::size_t)
            {
                burn(5000);
            },
            config
        );

    assert(cheap.available);
    assert(heavy.available);

    assert(
        heavy.trimmed_mean_ms_per_iteration >
        cheap.trimmed_mean_ms_per_iteration
    );

    const smart::FunctionProfile uniform =
        profiler.profile_index_range(
            0,
            900,
            [](std::size_t)
            {
                burn(800);
            },
            config
        );

    const smart::FunctionProfile regional =
        profiler.profile_index_range(
            0,
            900,
            [](std::size_t index)
            {
                if (index < 300)
                {
                    burn(100);
                }
                else if (index < 600)
                {
                    burn(1200);
                }
                else
                {
                    burn(6000);
                }
            },
            config
        );

    assert(uniform.available);
    assert(regional.available);
    assert(regional.spatial_observations_available);
    assert(regional.regional_cost_ratio > 1.0);

    assert(
        regional.regional_cost_ratio >=
        uniform.regional_cost_ratio
    );

    const smart::FunctionProfile tail =
        profiler.profile_index_range(
            0,
            1024,
            [](std::size_t index)
            {
                burn(index % 17 == 0 ? 12000 : 200);
            },
            config
        );

    assert(tail.available);
    assert(tail.tail_ratio >= 1.0);
    assert(tail.coefficient_of_variation >= 0.0);

    std::vector<int> small_values(256, 1);
    std::vector<int> large_values(4 * 1024 * 1024, 1);

    smart::WorkloadAnalyzer analyzer;

    const smart::WorkloadAnalysis small_analysis =
        analyzer.analyze(
            smart::WorkloadBuilder::container(small_values)
        );

    const smart::WorkloadAnalysis large_analysis =
        analyzer.analyze(
            smart::WorkloadBuilder::container(large_values)
        );

    assert(
        large_analysis.structural.represented_input_bytes >
        small_analysis.structural.represented_input_bytes
    );

    if (small_analysis.structural.cache_ratios_available &&
        large_analysis.structural.cache_ratios_available)
    {
        assert(
            large_analysis.structural.l3_residency_ratio >
            small_analysis.structural.l3_residency_ratio
        );
    }

    const smart::PerformanceModel small_model =
        smart::PerformanceModelBuilder().build(
            small_analysis
        );

    assert(
        small_model.workload.working_set_bytes ==
        small_analysis.structural.represented_input_bytes
    );

    if (small_analysis.structural.cache_ratios_available)
    {
        assert(
            small_model.used_structural_cache_observations
        );

        assert(
            small_model.l3_pressure ==
            small_analysis.structural.l3_residency_ratio
        );
    }

    return 0;
}