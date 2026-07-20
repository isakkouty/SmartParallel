#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <limits>
#include <smart/core/config.hpp>
#include <smart/execution/runtime_capabilities.hpp>
#include <smart/execution/thread_pool.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>
#include <thread>
#include <utility>
#include <vector>

namespace smart
{
struct BackendExecutionRequest
{
    std::size_t total = 0;
    std::size_t concurrency_budget = 1;
    std::size_t chunk_size = 0;
    std::function<void(std::size_t)> function;
};

struct BackendExecutionResult
{
    ExecutionEngineType backend = ExecutionEngineType::Auto;
    std::size_t effective_budget = 1;
    bool executed = false;
};

class IExecutionBackend
{
  public:
    virtual ~IExecutionBackend() = default;

    virtual ExecutionEngineType type() const noexcept = 0;
    virtual RuntimeCapabilities capabilities() const noexcept = 0;

    const char* name() const noexcept
    {
        return runtime_name(type());
    }

    virtual void execute_range(std::size_t total,
                               std::size_t job_count,
                               std::size_t chunk_size,
                               std::function<void(std::size_t)> func) = 0;

    void
    execute_range(std::size_t total, std::size_t job_count, std::function<void(std::size_t)> func)
    {
        execute_range(total, job_count, 0, std::move(func));
    }

    BackendExecutionResult execute(BackendExecutionRequest request)
    {
        BackendExecutionResult result;
        result.backend = type();
        result.effective_budget = std::max<std::size_t>(1, request.concurrency_budget);
        if (request.total == 0)
            return result;

        execute_range(request.total,
                      result.effective_budget,
                      request.chunk_size,
                      std::move(request.function));
        result.executed = true;
        return result;
    }
};

// Compatibility name retained for existing v1 users while the coordinator
// migrates to the backend-neutral contract.
using IExecutionEngine = IExecutionBackend;

class ThreadPoolEngine : public IExecutionBackend
{
  public:
    using IExecutionBackend::execute_range;

    ExecutionEngineType type() const noexcept override
    {
        return ExecutionEngineType::ThreadPool;
    }

    RuntimeCapabilities capabilities() const noexcept override
    {
        return RuntimeCapabilities{false, true, true, true, true, false, true};
    }

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

class OneTbbEngine : public IExecutionBackend
{
  public:
    using IExecutionBackend::execute_range;

    ExecutionEngineType type() const noexcept override
    {
        return ExecutionEngineType::OneTbb;
    }

    RuntimeCapabilities capabilities() const noexcept override
    {
        return RuntimeCapabilities{true, true, true, true, false, false, false};
    }

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

class StaticThreadEngine : public IExecutionBackend
{
  public:
    using IExecutionBackend::execute_range;

    ExecutionEngineType type() const noexcept override
    {
        return ExecutionEngineType::StaticThread;
    }

    RuntimeCapabilities capabilities() const noexcept override
    {
        return RuntimeCapabilities{false, false, true, false, false, false, false};
    }

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

inline IExecutionBackend& execution_backend(ExecutionEngineType type)
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

inline IExecutionBackend& execution_engine(ExecutionEngineType type)
{
    return execution_backend(type);
}

inline IExecutionBackend& default_execution_backend()
{
    return execution_backend(global_config().execution_engine);
}

inline IExecutionBackend& default_execution_engine()
{
    return default_execution_backend();
}
} // namespace smart
