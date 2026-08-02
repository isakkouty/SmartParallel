#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include <smart/core/config.hpp>
#include <smart/execution/nested_execution_session.hpp>
#include <smart/execution/runtime_capabilities.hpp>
#include <smart/execution/thread_pool.hpp>

#ifndef SMARTPARALLEL_HAS_TBB
#define SMARTPARALLEL_HAS_TBB 0
#endif

#if SMARTPARALLEL_HAS_TBB
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>
#endif

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
    std::uint64_t loop_id = 0;
    std::shared_ptr<NestedExecutionSession> nested_session;
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
    std::size_t observed_participating_threads = 0;
};

inline BackendExecutionResult& mutable_last_backend_execution_result() noexcept
{
    static thread_local BackendExecutionResult result;
    return result;
}

inline const BackendExecutionResult& last_backend_execution_result() noexcept
{
    return mutable_last_backend_execution_result();
}

inline void publish_backend_execution_result(const BackendExecutionResult& result) noexcept
{
    mutable_last_backend_execution_result() = result;
}

class IExecutionBackend
{
  public:
    virtual ~IExecutionBackend() = default;

    virtual ExecutionEngineType type() const noexcept = 0;
    virtual RuntimeCapabilities capabilities() const noexcept = 0;

    const char* name() const noexcept { return runtime_name(type()); }

    virtual void execute_range(std::size_t total,
                               std::size_t job_count,
                               std::size_t chunk_size,
                               std::function<void(std::size_t)> func) = 0;

    void execute_range(std::size_t total,
                       std::size_t job_count,
                       std::function<void(std::size_t)> func)
    {
        execute_range(total, job_count, 0, std::move(func));
    }

    virtual BackendExecutionResult execute(BackendExecutionRequest request)
    {
        BackendExecutionResult result;
        result.backend = type();
        result.effective_budget = std::max<std::size_t>(
            1, std::min(request.total == 0 ? std::size_t{1} : request.total,
                        request.concurrency_budget));
        if (request.total == 0)
        {
            publish_backend_execution_result(result);
            return result;
        }

        NestedExecutionSession::Lease lease;
        if (request.nested_session && effective_config().enable_nested_execution_session)
        {
            lease = request.nested_session->acquire(
                request.sequential_fallback ? 1 : result.effective_budget);
            result.effective_budget = std::max<std::size_t>(1, lease.participant_budget());
        }

        const auto function = std::move(request.function);
        const auto session = request.nested_session;
        std::mutex participants_mutex;
        std::set<std::thread::id> participants;
        const auto invoke = [&function, &session, &participants_mutex, &participants](std::size_t i)
        {
            {
                std::lock_guard<std::mutex> lock(participants_mutex);
                participants.insert(std::this_thread::get_id());
            }
            NestedExecutionSession::ParticipantScope participant(session.get());
            function(i);
        };
        const auto finalize_result = [&]()
        {
            std::lock_guard<std::mutex> lock(participants_mutex);
            result.observed_participating_threads = participants.size();
            publish_backend_execution_result(result);
        };

        if (request.nested_session)
            request.nested_session->update_backend_trace(
                request.loop_id, type(), result.effective_budget, result.effective_budget,
                request.sequential_fallback ? 1 : result.effective_budget, false, false);

        if (request.sequential_fallback || result.effective_budget <= 1)
        {
            result.effective_budget = 1;
            result.runtime_concurrency = 1;
            result.sequential_fallback = true;
            for (std::size_t i = 0; i < request.total; ++i)
                invoke(i);
            result.executed = true;
            finalize_result();
            return result;
        }

        execute_range(request.total,
                      result.effective_budget,
                      request.chunk_size,
                      invoke);
        result.runtime_concurrency = result.effective_budget;
        result.spawned_workers = result.effective_budget;
        result.executed = true;
        finalize_result();
        return result;
    }
};

using IExecutionEngine = IExecutionBackend;

inline std::atomic<std::uint64_t>& one_tbb_execution_counter() noexcept
{
    static std::atomic<std::uint64_t> counter{0};
    return counter;
}

inline void reset_one_tbb_execution_count() noexcept
{
    one_tbb_execution_counter().store(0, std::memory_order_release);
}

inline std::uint64_t one_tbb_execution_count() noexcept
{
    return one_tbb_execution_counter().load(std::memory_order_acquire);
}

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
        return RuntimeCapabilities{false, true, true, true, true, true, true};
    }

    BackendExecutionResult execute(BackendExecutionRequest request) override
    {
        BackendExecutionResult result;
        result.backend = type();
        result.effective_budget = std::max<std::size_t>(
            1, std::min({request.concurrency_budget,
                         request.total == 0 ? std::size_t{1} : request.total,
                         global_thread_pool().thread_count()}));
        if (request.total == 0)
        {
            publish_backend_execution_result(result);
            return result;
        }

        ThreadPool& pool = global_thread_pool();
        const bool cooperative_reentry = request.cooperative_helping || pool.is_worker_thread();

        std::size_t requested_workers = request.sequential_fallback ? 1 : result.effective_budget;
        const std::size_t target_chunks = requested_workers > 0
            && requested_workers <= std::numeric_limits<std::size_t>::max() / 4
            ? requested_workers * 4
            : std::numeric_limits<std::size_t>::max();
        const std::size_t grain = std::max<std::size_t>(
            1, request.chunk_size == 0
                   ? 1 + (request.total - 1) / std::max<std::size_t>(1, target_chunks)
                   : request.chunk_size);
        SchedulerVisibleWork work(0, request.total, grain, current_execution_context());

        NestedExecutionSession::Lease caller_lease;
        std::vector<std::shared_ptr<void>> helper_lifetimes;
        std::size_t helper_limit = requested_workers > 1
            ? pool.recommended_helper_count(work, requested_workers)
            : 0;
        if (request.nested_session && effective_config().enable_nested_execution_session)
        {
            const bool caller_owned = request.nested_session->current_thread_owns_participant();
            if (!caller_owned)
                caller_lease = request.nested_session->reserve(1);
            if (!caller_owned && !caller_lease.valid())
            {
                result.sequential_fallback = true;
                requested_workers = 1;
                helper_limit = 0;
            }

            helper_lifetimes.reserve(helper_limit);
            for (std::size_t helper = 0; helper < helper_limit; ++helper)
            {
                auto helper_lease = request.nested_session->reserve(1);
                if (!helper_lease.valid())
                    break;
                helper_lifetimes.push_back(
                    std::make_shared<NestedExecutionSession::Lease>(std::move(helper_lease)));
            }
            helper_limit = helper_lifetimes.size();
        }

        const std::size_t workers = 1 + helper_limit;
        result.effective_budget = workers;
        result.runtime_concurrency = workers;
        if (request.nested_session)
            request.nested_session->update_backend_trace(
                request.loop_id, type(), workers, workers, workers, false, cooperative_reentry);

        const auto function = std::move(request.function);
        const auto session = request.nested_session;
        std::mutex participants_mutex;
        std::set<std::thread::id> participants;
        const auto invoke = [&function, &session, &participants_mutex, &participants](std::size_t i)
        {
            {
                std::lock_guard<std::mutex> lock(participants_mutex);
                participants.insert(std::this_thread::get_id());
            }
            NestedExecutionSession::ParticipantScope participant(session.get());
            function(i);
        };
        const auto finalize_result = [&]()
        {
            std::lock_guard<std::mutex> lock(participants_mutex);
            result.observed_participating_threads = participants.size();
            publish_backend_execution_result(result);
        };

        if (request.sequential_fallback || workers <= 1)
        {
            result.sequential_fallback = true;
            for (std::size_t i = 0; i < request.total; ++i)
                invoke(i);
            result.executed = true;
            finalize_result();
            return result;
        }

        // Use dependency-local cooperative execution for root and nested regions.
        // The caller is one participant, helpers are recruited only when useful,
        // and completion never waits for unrelated jobs in the global pool.
        const auto helping = pool.execute_visible_work_helping(
            work,
            workers,
            [&invoke, &work](const WorkChunk& chunk)
            {
                for (std::size_t i = chunk.begin; i < chunk.end; ++i)
                {
                    if (work.cancelled())
                        break;
                    invoke(i);
                }
            },
            helper_limit,
            std::move(helper_lifetimes));
        if (request.nested_session)
            request.nested_session->update_scheduler_trace(
                request.loop_id,
                workers,
                workers,
                work.total_chunks(),
                helping);
        result.spawned_workers = helping.helper_jobs_started;
        result.reused_runtime_domain = cooperative_reentry;

        result.executed = true;
        finalize_result();
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
#if SMARTPARALLEL_HAS_TBB
        return RuntimeCapabilities{true, true, true, true, false, true, false};
#else
        return RuntimeCapabilities{};
#endif
    }

    BackendExecutionResult execute(BackendExecutionRequest request) override
    {
#if SMARTPARALLEL_HAS_TBB
        one_tbb_execution_counter().fetch_add(1, std::memory_order_relaxed);
        BackendExecutionResult result;
        result.backend = type();
        result.effective_budget = std::max<std::size_t>(
            1, std::min(request.total == 0 ? std::size_t{1} : request.total,
                        request.concurrency_budget));
        if (request.total == 0)
        {
            publish_backend_execution_result(result);
            return result;
        }

        const bool inside_tbb_domain =
            tbb::this_task_arena::current_thread_index() != tbb::task_arena::not_initialized;
        std::size_t domain_concurrency = 0;
        if (request.native_delegation && inside_tbb_domain)
        {
            domain_concurrency = std::max<std::size_t>(
                1, static_cast<std::size_t>(tbb::this_task_arena::max_concurrency()));
            result.effective_budget = std::min(result.effective_budget, domain_concurrency);
        }
        NestedExecutionSession::Lease lease;
        if (request.nested_session && effective_config().enable_nested_execution_session)
        {
            lease = request.nested_session->acquire(
                request.sequential_fallback ? 1 : result.effective_budget);
            result.effective_budget = std::max<std::size_t>(1, lease.participant_budget());
        }

        const auto function = std::move(request.function);
        const auto session = request.nested_session;
        std::mutex participants_mutex;
        std::set<std::thread::id> participants;
        const auto invoke = [&function, &session, &participants_mutex, &participants](std::size_t i)
        {
            {
                std::lock_guard<std::mutex> lock(participants_mutex);
                participants.insert(std::this_thread::get_id());
            }
            NestedExecutionSession::ParticipantScope participant(session.get());
            function(i);
        };
        const auto finalize_result = [&]()
        {
            std::lock_guard<std::mutex> lock(participants_mutex);
            result.observed_participating_threads = participants.size();
            publish_backend_execution_result(result);
        };

        const bool native_delegation = request.native_delegation && inside_tbb_domain;
        const bool reuse_native_domain = native_delegation
            && result.effective_budget >= domain_concurrency;
        if (request.nested_session)
            request.nested_session->update_backend_trace(
                request.loop_id, type(), result.effective_budget, result.effective_budget,
                request.sequential_fallback ? 1 : result.effective_budget,
                native_delegation, reuse_native_domain);

        if (request.sequential_fallback || result.effective_budget <= 1)
        {
            result.effective_budget = 1;
            result.runtime_concurrency = 1;
            result.sequential_fallback = true;
            for (std::size_t i = 0; i < request.total; ++i)
                invoke(i);
            result.executed = true;
            finalize_result();
            return result;
        }

        if (reuse_native_domain)
        {
            result.runtime_concurrency = result.effective_budget;
            result.native_delegation = true;
            result.reused_runtime_domain = true;
            execute_parallel_for(request.total, request.chunk_size, invoke);
            result.executed = true;
            finalize_result();
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
                                     invoke);
            });
        result.executed = true;
        result.native_delegation = request.native_delegation && inside_tbb_domain;
        result.reused_runtime_domain = false;
        finalize_result();
        return result;
#else
        // This object is normally unreachable because execution_backend() resolves
        // unavailable oneTBB requests to ThreadPool. Keep a safe fallback for code
        // that directly instantiates OneTbbEngine.
        ThreadPoolEngine fallback;
        return fallback.execute(std::move(request));
#endif
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
#if SMARTPARALLEL_HAS_TBB
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
#endif
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
        return RuntimeCapabilities{false, false, true, false, false, true, false};
    }

    BackendExecutionResult execute(BackendExecutionRequest request) override
    {
        BackendExecutionResult result;
        result.backend = type();
        result.effective_budget = std::max<std::size_t>(
            1, std::min(request.total == 0 ? std::size_t{1} : request.total,
                        request.concurrency_budget));
        if (request.total == 0)
        {
            publish_backend_execution_result(result);
            return result;
        }

        const bool caller_owned = request.nested_session
            && request.nested_session->current_thread_owns_participant();
        if (caller_owned && result.effective_budget > 1)
            request.sequential_fallback = true;

        NestedExecutionSession::Lease lease;
        if (request.nested_session && effective_config().enable_nested_execution_session)
        {
            lease = request.nested_session->acquire(
                request.sequential_fallback ? 1 : result.effective_budget);
            result.effective_budget = std::max<std::size_t>(1, lease.participant_budget());
        }

        const auto function = std::move(request.function);
        const auto session = request.nested_session;
        std::mutex participants_mutex;
        std::set<std::thread::id> participants;
        const auto invoke = [&function, &session, &participants_mutex, &participants](std::size_t i)
        {
            {
                std::lock_guard<std::mutex> lock(participants_mutex);
                participants.insert(std::this_thread::get_id());
            }
            NestedExecutionSession::ParticipantScope participant(session.get());
            function(i);
        };
        const auto finalize_result = [&]()
        {
            std::lock_guard<std::mutex> lock(participants_mutex);
            result.observed_participating_threads = participants.size();
            publish_backend_execution_result(result);
        };

        if (request.nested_session)
            request.nested_session->update_backend_trace(
                request.loop_id, type(), result.effective_budget, result.effective_budget,
                request.sequential_fallback ? 1 : result.effective_budget, false, false);

        if (request.sequential_fallback || result.effective_budget <= 1)
        {
            result.effective_budget = 1;
            result.runtime_concurrency = 1;
            result.sequential_fallback = true;
            for (std::size_t i = 0; i < request.total; ++i)
                invoke(i);
            result.executed = true;
            finalize_result();
            return result;
        }

        const std::size_t workers =
            std::max<std::size_t>(1, std::min(request.total, result.effective_budget));
        result.effective_budget = workers;
        result.runtime_concurrency = workers;
        result.spawned_workers = workers;
        execute_range(request.total, workers, request.chunk_size, invoke);
        result.executed = true;
        finalize_result();
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
        if (job_count <= 1)
        {
            for (std::size_t i = 0; i < total; ++i)
                func(i);
            return;
        }

        std::vector<std::thread> threads;
        threads.reserve(job_count);
        std::atomic<bool> cancelled{false};
        std::mutex exception_mutex;
        std::exception_ptr first_exception;

        try
        {
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
                            for (std::size_t i = begin;
                                 i < end && !cancelled.load(std::memory_order_acquire);
                                 ++i)
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
        }
        catch (...)
        {
            cancelled.store(true, std::memory_order_release);
            for (std::thread& thread : threads)
                if (thread.joinable())
                    thread.join();
            throw;
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
        return execution_backend_available(type) ? static_cast<IExecutionBackend&>(one_tbb_engine)
                                                 : static_cast<IExecutionBackend&>(thread_pool_engine);
    return thread_pool_engine;
}

inline IExecutionBackend& execution_engine(ExecutionEngineType type)
{
    return execution_backend(type);
}

inline IExecutionBackend& default_execution_backend()
{
    return execution_backend(effective_config().execution_engine);
}

inline IExecutionBackend& default_execution_engine()
{
    return default_execution_backend();
}
} // namespace smart
