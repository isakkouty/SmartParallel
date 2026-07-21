#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <limits>
#include <exception>
#include <mutex>
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
    bool native_delegation = false;
    bool cooperative_helping = false;
    bool sequential_fallback = false;
    std::function<void(std::size_t)> function;
};

struct BackendExecutionResult
{
    ExecutionEngineType backend = ExecutionEngineType::Auto;
    std::size_t effective_budget = 1;
    std::size_t runtime_concurrency = 1;
    bool executed = false;
    bool native_delegation = false;
    bool reused_runtime_domain = false;
    bool sequential_fallback = false;
    std::size_t spawned_workers = 0;
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

    virtual BackendExecutionResult execute(BackendExecutionRequest request)
    {
        BackendExecutionResult result;
        result.backend = type();
        result.effective_budget = std::max<std::size_t>(1, request.concurrency_budget);
        if (request.total == 0)
            return result;

        if (request.sequential_fallback)
        {
            result.effective_budget = 1;
            result.runtime_concurrency = 1;
            result.sequential_fallback = true;
            for (std::size_t i = 0; i < request.total; ++i)
                request.function(i);
            result.executed = true;
            return result;
        }

        execute_range(request.total,
                      result.effective_budget,
                      request.chunk_size,
                      std::move(request.function));
        result.runtime_concurrency = result.effective_budget;
        result.spawned_workers = result.effective_budget;
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

    BackendExecutionResult execute(BackendExecutionRequest request) override
    {
        BackendExecutionResult result;
        result.backend = type();
        result.effective_budget = std::max<std::size_t>(1, request.concurrency_budget);
        if (request.total == 0)
            return result;

        ThreadPool& pool = global_thread_pool();

        // A ThreadPool worker must never enter the root-style execution path,
        // even when an intermediate profiling or sequential region caused the
        // coordinator to classify the call as direct execution. Root execution
        // waits for the pool's global queue to drain; doing that from a worker
        // can deadlock when every worker is recursively waiting. Re-entry from
        // an owned worker is therefore always upgraded to dependency-local
        // cooperative helping.
        const bool cooperative_reentry =
            request.cooperative_helping || pool.is_worker_thread();
        if (!cooperative_reentry)
            return IExecutionBackend::execute(std::move(request));

        const std::size_t workers =
            std::max<std::size_t>(1, std::min({result.effective_budget, request.total, pool.thread_count()}));
        const std::size_t grain = std::max<std::size_t>(
            1, request.chunk_size == 0
                   ? (request.total + workers * 4 - 1) / (workers * 4)
                   : request.chunk_size);

        SchedulerVisibleWork work(0, request.total, grain, current_execution_context());
        const auto function = std::move(request.function);
        pool.execute_visible_work_helping(
            work,
            workers,
            [&function](const WorkChunk& chunk)
            {
                for (std::size_t i = chunk.begin; i < chunk.end; ++i)
                    function(i);
            });

        result.executed = true;
        return result;
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
        SchedulerVisibleWork work(0, total, grain, current_execution_context());
        pool.execute_visible_work(
            work, workers,
            [&func](const WorkChunk& chunk)
            {
                for (std::size_t i = chunk.begin; i < chunk.end; ++i)
                    func(i);
            });
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

    BackendExecutionResult execute(BackendExecutionRequest request) override
    {
        BackendExecutionResult result;
        result.backend = type();
        result.effective_budget = std::max<std::size_t>(1, request.concurrency_budget);
        if (request.total == 0)
            return result;

        const bool inside_tbb_domain =
            tbb::this_task_arena::current_thread_index() != tbb::task_arena::not_initialized;

        if (request.native_delegation && inside_tbb_domain)
        {
            const std::size_t domain_concurrency = std::max<std::size_t>(
                1, static_cast<std::size_t>(tbb::this_task_arena::max_concurrency()));
            result.runtime_concurrency = domain_concurrency;
            result.effective_budget = std::min(result.effective_budget, domain_concurrency);
            result.native_delegation = true;
            result.reused_runtime_domain = true;
            execute_parallel_for(request.total,
                                 request.chunk_size,
                                 std::move(request.function));
            result.executed = true;
            return result;
        }

        const std::size_t workers =
            std::max<std::size_t>(1, std::min(request.total, result.effective_budget));
        result.effective_budget = workers;
        result.runtime_concurrency = workers;

        const int arena_workers = static_cast<int>(std::min<std::size_t>(
            workers, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        tbb::task_arena arena(arena_workers);
        arena.execute(
            [&]()
            {
                execute_parallel_for(request.total,
                                     request.chunk_size,
                                     std::move(request.function));
            });

        result.executed = true;
        return result;
    }

    void execute_range(std::size_t total,
                       std::size_t job_count,
                       std::size_t chunk_size,
                       std::function<void(std::size_t)> func) override
    {
        BackendExecutionRequest request;
        request.total = total;
        request.concurrency_budget = job_count;
        request.chunk_size = chunk_size;
        request.function = std::move(func);
        execute(std::move(request));
    }

  private:
    static void execute_parallel_for(std::size_t total,
                                     std::size_t chunk_size,
                                     std::function<void(std::size_t)> func)
    {
        const std::size_t grain = std::max<std::size_t>(1, chunk_size == 0 ? 1 : chunk_size);
        tbb::parallel_for(tbb::blocked_range<std::size_t>(0, total, grain),
                          [&](const tbb::blocked_range<std::size_t>& range)
                          {
                              for (std::size_t i = range.begin(); i < range.end(); ++i)
                                  func(i);
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

    BackendExecutionResult execute(BackendExecutionRequest request) override
    {
        BackendExecutionResult result;
        result.backend = type();
        result.effective_budget = std::max<std::size_t>(1, request.concurrency_budget);
        if (request.total == 0)
            return result;

        if (request.sequential_fallback)
        {
            result.effective_budget = 1;
            result.runtime_concurrency = 1;
            result.sequential_fallback = true;
            for (std::size_t i = 0; i < request.total; ++i)
                request.function(i);
            result.executed = true;
            return result;
        }

        const std::size_t workers =
            std::max<std::size_t>(1, std::min(request.total, result.effective_budget));
        result.effective_budget = workers;
        result.runtime_concurrency = workers;
        result.spawned_workers = workers;
        execute_range(request.total, workers, request.chunk_size, std::move(request.function));
        result.executed = true;
        return result;
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
        std::atomic<bool> cancelled{false};
        std::mutex exception_mutex;
        std::exception_ptr first_exception;

        for (std::size_t t = 0; t < job_count; ++t)
        {
            const std::size_t base = total / job_count;
            const std::size_t remainder = total % job_count;
            const std::size_t begin = t * base + std::min(t, remainder);
            const std::size_t end = begin + base + (t < remainder ? 1 : 0);

            threads.emplace_back(
                [begin, end, &func, &cancelled, &exception_mutex, &first_exception]()
                {
                    try
                    {
                        for (std::size_t i = begin; i < end && !cancelled.load(std::memory_order_acquire); ++i)
                            func(i);
                    }
                    catch (...)
                    {
                        {
                            std::lock_guard<std::mutex> lock(exception_mutex);
                            if (!first_exception)
                                first_exception = std::current_exception();
                        }
                        cancelled.store(true, std::memory_order_release);
                    }
                });
        }

        for (std::thread& thread : threads)
            thread.join();
        if (first_exception)
            std::rethrow_exception(first_exception);
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
