#include <smart/profiling/function_profiler.hpp>

#include <cassert>
#include <cstddef>

namespace
{
    void burn(std::size_t iterations)
    {
        volatile std::size_t value = 1;
        for (std::size_t i = 0; i < iterations; ++i)
        {
            value = value * 1664525u + 1013904223u + i;
        }
    }
}

int main()
{
    smart::FunctionProfiler profiler;

    smart::FunctionProfiler::Config config;
    config.min_samples = 10;
    config.max_samples = 18;
    config.batch_size = 1;
    config.max_batch_size = 32;
    config.max_callback_invocations = 512;
    config.local_sample_count = 3;
    config.target_batch_duration_ms = 0.01;
    config.max_profile_time_ms = 100.0;
    config.relative_error_target = 0.001;
    config.warmup_ratio_threshold = 1.25;

    const smart::FunctionProfile spatial_profile =
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
                    burn(1500);
                }
                else
                {
                    burn(6000);
                }
            },
            config);

    assert(spatial_profile.available);
    assert(spatial_profile.spatial_observations_available);
    assert(spatial_profile.local_samples > 0);
    assert(spatial_profile.distributed_samples > 0);
    assert(spatial_profile.early_region_median_ms_per_iteration > 0.0);
    assert(spatial_profile.middle_region_median_ms_per_iteration > 0.0);
    assert(spatial_profile.late_region_median_ms_per_iteration > 0.0);
    assert(
        spatial_profile.late_region_median_ms_per_iteration >
        spatial_profile.early_region_median_ms_per_iteration);
    assert(spatial_profile.regional_cost_ratio > 1.0);

    bool initialized = false;
    const smart::FunctionProfile warmup_profile =
        profiler.profile_index_range(
            0,
            256,
            [&](std::size_t)
            {
                if (!initialized)
                {
                    burn(200000);
                    initialized = true;
                }
                else
                {
                    burn(300);
                }
            },
            config);

    assert(warmup_profile.available);
    assert(warmup_profile.first_batch_size >= 1);
    assert(warmup_profile.first_batch_ms_per_iteration > 0.0);
    assert(warmup_profile.steady_state_ms_per_iteration > 0.0);
    assert(warmup_profile.warmup_ratio > 1.0);
    assert(warmup_profile.estimated_setup_cost_ms > 0.0);
    assert(warmup_profile.warmup_detected);

    const smart::FunctionProfile repeat_profile =
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
                    burn(1500);
                }
                else
                {
                    burn(6000);
                }
            },
            config);

    assert(repeat_profile.available);
    assert(repeat_profile.local_samples > 0);
    assert(repeat_profile.distributed_samples > 0);
    assert(repeat_profile.regional_cost_ratio > 1.0);

    return 0;
}
