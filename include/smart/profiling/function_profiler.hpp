#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <numeric>
#include <vector>

namespace smart
{
    struct FunctionProfile
    {
        bool available = false;

        std::size_t samples = 0;

        double avg_ms_per_iteration = 0.0;
        double p95_ms_per_iteration = 0.0;
        double max_ms_per_iteration = 0.0;

        double estimated_total_work_ms = 0.0;
        double estimated_parallel_overhead_ms = 0.0;
        double parallel_worthiness = 0.0;

        double instability_ratio = 0.0;
        bool stable = false;
    };

    class FunctionProfiler
    {
    public:
        struct Config
        {
            std::size_t min_samples = 8;
            std::size_t max_samples = 64;
            std::size_t batch_size = 64;

            double measured_parallel_overhead_ms = 1.0;
            double stable_ratio = 2.5;
        };

        template <typename Function>
        FunctionProfile profile_index_range(
            std::size_t begin,
            std::size_t end,
            Function&& func,
            Config config = {}) const
        {
            FunctionProfile profile;

            if (end <= begin)
                return profile;

            std::size_t total = end - begin;

            std::size_t sample_count =
                std::min(config.max_samples, total);

            sample_count =
                std::max<std::size_t>(
                    std::min(config.min_samples, total),
                    sample_count
                );

            std::vector<double> samples;
            samples.reserve(sample_count);
            
            for (std::size_t s = 0; s < sample_count; ++s)
            {
                std::size_t start_index =
                    begin + distributed_index(s, sample_count, total);

                std::size_t batch_count =
                    std::min(config.batch_size, end - start_index);

                auto start = std::chrono::steady_clock::now();

                for (std::size_t b = 0; b < batch_count; ++b)
                {
                    func(start_index + b);
                }

                auto stop = std::chrono::steady_clock::now();

                double ms =
                    std::chrono::duration<double, std::milli>(
                        stop - start
                    ).count();

                samples.push_back(ms / static_cast<double>(batch_count));
            }

            std::sort(samples.begin(), samples.end());

            double sum =
                std::accumulate(samples.begin(), samples.end(), 0.0);

            profile.available = true;
            profile.samples = samples.size();
            profile.avg_ms_per_iteration = sum / samples.size();
            profile.max_ms_per_iteration = samples.back();

            std::size_t p95_index =
                std::min(
                    samples.size() - 1,
                    static_cast<std::size_t>(
                        (samples.size() - 1) * 0.95
                    )
                );

            profile.p95_ms_per_iteration = samples[p95_index];

            profile.estimated_total_work_ms =
                profile.avg_ms_per_iteration * static_cast<double>(total);

            profile.estimated_parallel_overhead_ms =
                config.measured_parallel_overhead_ms;

            if (profile.estimated_parallel_overhead_ms > 0.0)
            {
                profile.parallel_worthiness =
                    profile.estimated_total_work_ms /
                    profile.estimated_parallel_overhead_ms;
            }

            if (profile.avg_ms_per_iteration > 0.0)
            {
                profile.instability_ratio =
                    profile.max_ms_per_iteration /
                    profile.avg_ms_per_iteration;
            }

            profile.stable =
                profile.instability_ratio <= config.stable_ratio;

            return profile;
        }

    private:
        static std::size_t distributed_index(
            std::size_t sample,
            std::size_t sample_count,
            std::size_t total)
        {
            if (sample_count <= 1)
                return 0;

            return sample * (total - 1) / (sample_count - 1);
        }
    };
}
