#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <limits>
#include <smart/core/config.hpp>
#include <smart/execution/thread_pool.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>
#include <thread>
#include <utility>
#include <vector>

namespace smart
{
class IExecutionEngine
{
  public:
    virtual ~IExecutionEngine() = default;

    virtual void execute_range(std::size_t total,
                               std::size_t job_count,
                               std::size_t chunk_size,
                               std::function<void(std::size_t)> func) = 0;

    void
    execute_range(std::size_t total, std::size_t job_count, std::function<void(std::size_t)> func)
    {
        execute_range(total, job_count, 0, std::move(func));
    }
};

class ThreadPoolEngine : public IExecutionEngine
{
  public:
    using IExecutionEngine::execute_range;

    void execute_range(std::size_t total,
                       std::size_t job_count,
                       std::size_t chunk_size,
                       std::function<void(std::size_t)> func) override
    {
        if (total == 0)
            return;

        ThreadPool& pool = global_thread_pool();
        const std::size_t workers =
            std::max<std::size_t>(1, std::min({job_count, total, pool.thread_count()}));
        const std::size_t grain = std::max<std::size_t>(
            1, chunk_size == 0 ? (total + workers * 4 - 1) / (workers * 4) : chunk_size);

        std::atomic<std::size_t> next{0};

        for (std::size_t worker = 0; worker < workers; ++worker)
        {
            pool.submit(
                [total, grain, &next, func]()
                {
                    while (true)
                    {
                        const std::size_t begin = next.fetch_add(grain, std::memory_order_relaxed);
                        if (begin >= total)
                            break;

                        const std::size_t end = std::min(total, begin + grain);
                        for (std::size_t i = begin; i < end; ++i)
                        {
                            func(i);
                        }
                    }
                });
        }

        pool.wait();
    }
};

class OneTbbEngine : public IExecutionEngine
{
  public:
    using IExecutionEngine::execute_range;

    void execute_range(std::size_t total,
                       std::size_t job_count,
                       std::size_t chunk_size,
                       std::function<void(std::size_t)> func) override
    {
        if (total == 0)
            return;

        const std::size_t workers = std::max<std::size_t>(1, std::min(total, job_count));
        const std::size_t grain = std::max<std::size_t>(1, chunk_size == 0 ? 1 : chunk_size);
        const int arena_workers = static_cast<int>(std::min<std::size_t>(
            workers, static_cast<std::size_t>(std::numeric_limits<int>::max())));

        tbb::task_arena arena(arena_workers);
        arena.execute(
            [&]()
            {
                tbb::parallel_for(tbb::blocked_range<std::size_t>(0, total, grain),
                                  [&](const tbb::blocked_range<std::size_t>& range)
                                  {
                                      for (std::size_t i = range.begin(); i < range.end(); ++i)
                                      {
                                          func(i);
                                      }
                                  });
            });
    }
};

class StaticThreadEngine : public IExecutionEngine
{
  public:
    using IExecutionEngine::execute_range;

    void execute_range(std::size_t total,
                       std::size_t job_count,
                       std::size_t,
                       std::function<void(std::size_t)> func) override
    {
        if (total == 0)
            return;

        job_count = std::max<std::size_t>(1, std::min(job_count, total));

        std::vector<std::thread> threads;
        threads.reserve(job_count);

        for (std::size_t t = 0; t < job_count; ++t)
        {
            const std::size_t base = total / job_count;
            const std::size_t remainder = total % job_count;
            const std::size_t begin = t * base + std::min(t, remainder);
            const std::size_t end = begin + base + (t < remainder ? 1 : 0);

            threads.emplace_back(
                [begin, end, func]()
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
};

inline IExecutionEngine& execution_engine(ExecutionEngineType type)
{
    static ThreadPoolEngine thread_pool_engine;
    static OneTbbEngine one_tbb_engine;
    static StaticThreadEngine static_thread_engine;

    if (type == ExecutionEngineType::StaticThread)
        return static_thread_engine;
    if (type == ExecutionEngineType::OneTbb)
        return one_tbb_engine;
    return thread_pool_engine;
}

inline IExecutionEngine& default_execution_engine()
{
    return execution_engine(global_config().execution_engine);
}
} // namespace smart
