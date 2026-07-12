#pragma once

#include <cstddef>
#include <thread>
#include <vector>

namespace smart
{
    template <typename Container, typename Function>
    void static_thread_for_each(
        Container& container,
        std::size_t job_count,
        Function func
    )
    {
        std::size_t total = static_cast<std::size_t>(container.size());

        if (total == 0)
            return;

        if (job_count > total)
            job_count = total;

        std::vector<std::thread> threads;
        threads.reserve(job_count);

        for (std::size_t t = 0; t < job_count; ++t)
        {
            std::size_t begin = (total * t) / job_count;
            std::size_t end = (total * (t + 1)) / job_count;

            threads.emplace_back([begin, end, &container, &func]()
            {
                for (std::size_t i = begin; i < end; ++i)
                {
                    func(container[i]);
                }
            });
        }

        for (std::thread& thread : threads)
        {
            thread.join();
        }
    }
}
