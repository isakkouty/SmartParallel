#pragma once

#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

namespace smart
{
    template <typename Function>
    void static_thread_execute_range(
        std::size_t total,
        std::size_t job_count,
        Function func
    )
    {
        if (total == 0)
            return;

        if (job_count == 0)
            job_count = 1;

        if (job_count > total)
            job_count = total;

        std::vector<std::thread> threads;
        threads.reserve(job_count);

        for (std::size_t t = 0; t < job_count; ++t)
        {
            const std::size_t base_size = total / job_count;
            const std::size_t remainder = total % job_count;

            const std::size_t begin =
                t * base_size + std::min(t, remainder);

            const std::size_t end =
                begin + base_size + (t < remainder ? 1 : 0);

            threads.emplace_back([begin, end, &func]()
            {
                for (std::size_t i = begin; i < end; ++i)
                {
                    func(i);
                }
            });
        }

        for (std::thread& thread : threads)
        {
            thread.join();
        }
    }
}
