#include <smart/profiling/function_profiler.hpp>

#include <cassert>
#include <cstddef>

namespace
{
    void burn(std::size_t iterations)
    {
        volatile std::size_t sink = 7;
        for (std::size_t i = 0; i < iterations; ++i)
        {
            sink = sink * 1103515245u + 12345u + i;
        }
    }
}

int main()
{
    smart::FunctionProfiler profiler;
    smart::FunctionProfiler::Config config;
    config.min_samples = 4;
    config.max_samples = 32;
    config.batch_size = 1;
    config.max_batch_size = 128;
    config.max_callback_invocations = 256;
    config.max_profile_time_ms = 20.0;
    config.target_batch_duration_ms = 0.01;
    config.relative_error_target = 0.05;

    const smart::FunctionProfile stable =
        profiler.profile_index_range(
            0,
            4096,
            [](std::size_t)
            {
                burn(800);
            },
            config);

    assert(stable.available);
    assert(stable.callback_invocations <= config.max_callback_invocations);
    assert(stable.measured_batches <= config.max_samples);
    assert(stable.profiling_elapsed_ms <= config.max_profile_time_ms + 5.0);
    assert(stable.stop_reason != smart::ProfileStopReason::None);

    const smart::FunctionProfile noisy =
        profiler.profile_index_range(
            0,
            4096,
            [](std::size_t index)
            {
                burn(index % 2 == 0 ? 100 : 5000);
            },
            config);

    assert(noisy.available);
    assert(noisy.callback_invocations <= config.max_callback_invocations);
    assert(noisy.measured_batches <= config.max_samples);
    assert(noisy.stop_reason != smart::ProfileStopReason::None);
    assert(noisy.coefficient_of_variation >= 0.0);

    return 0;
}
