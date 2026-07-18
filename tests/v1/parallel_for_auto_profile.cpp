#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include <smart/execution/parallel.hpp>

int main()
{
    smart::global_config().enable_experience = false;
    smart::global_config().enable_utility_model_runtime = false;
    smart::global_config().enable_parallel_for_auto_profiling = true;

    constexpr std::size_t count = 4096;
    std::vector<std::atomic<unsigned>> visits(count);
    for (auto& value : visits)
    {
        value.store(0);
    }

    smart::parallel_for(0, count, [&](std::size_t i)
    {
        visits[i].fetch_add(1, std::memory_order_relaxed);
        volatile double sink = 0.0;
        for (unsigned k = 0; k < 200; ++k)
        {
            sink += static_cast<double>((i + 1) * (k + 3)) * 0.000001;
        }
        (void)sink;
    });

    for (const auto& value : visits)
    {
        if (value.load(std::memory_order_relaxed) != 1)
        {
            throw std::runtime_error("parallel_for duplicated or skipped work");
        }
    }

    return 0;
}
