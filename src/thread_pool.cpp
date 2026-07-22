#include <smart/execution/thread_pool.hpp>
#include <smart/core/config.hpp>
#include <smart/hardware/hardware.hpp>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <utility>

namespace smart
{
namespace
{
thread_local const ThreadPool* active_worker_pool = nullptr;
thread_local std::size_t active_worker_job_depth = 0;

class ActivePoolJobScope
{
  public:
    explicit ActivePoolJobScope(const ThreadPool* pool) noexcept
        : previous_pool_(active_worker_pool), previous_depth_(active_worker_job_depth)
    {
        if (active_worker_pool == pool)
        {
            ++active_worker_job_depth;
        }
        else
        {
            active_worker_pool = pool;
            active_worker_job_depth = 1;
        }
    }

    ActivePoolJobScope(const ActivePoolJobScope&) = delete;
    ActivePoolJobScope& operator=(const ActivePoolJobScope&) = delete;

    ~ActivePoolJobScope()
    {
        active_worker_pool = previous_pool_;
        active_worker_job_depth = previous_depth_;
    }

  private:
    const ThreadPool* previous_pool_ = nullptr;
    std::size_t previous_depth_ = 0;
};
}
ThreadPool::ThreadPool(std::size_t thread_count)
    : thread_count_(std::max<std::size_t>(1, thread_count))
{
    for (std::size_t i = 0; i < thread_count_; ++i)
    {
        workers_.emplace_back(
            [this]()
            {
                while (true)
                {
                    std::function<void()> job;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        condition_.wait(lock,
                                        [this]()
                                        {
                                            return stop_ || !jobs_.empty();
                                        });

                        if (stop_ && jobs_.empty())
                            return;

                        job = std::move(jobs_.front().function);
                        jobs_.pop_front();
                        ++busy_workers_;
                    }

                    {
                        ActivePoolJobScope active_job(this);
                        try
                        {
                            job();
                        }
                        catch (...)
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            if (!unhandled_exception_)
                                unhandled_exception_ = std::current_exception();
                        }
                    }
                    finish_one_job(true);
                }
            });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        stop_ = true;
    }

    condition_.notify_all();

    for (std::thread& worker : workers_)
    {
        if (worker.joinable())
            worker.join();
    }
}

std::size_t ThreadPool::thread_count() const
{
    return thread_count_;
}

std::size_t ThreadPool::idle_worker_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return busy_workers_ >= thread_count_ ? 0 : thread_count_ - busy_workers_;
}

std::size_t ThreadPool::active_job_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return active_jobs_;
}

std::size_t ThreadPool::queued_job_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size();
}

std::size_t ThreadPool::busy_worker_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return busy_workers_;
}

bool ThreadPool::shutting_down() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stop_;
}

bool ThreadPool::is_worker_thread() const noexcept
{
    return active_worker_pool == this;
}

void ThreadPool::submit(std::function<void()> job)
{
    if (!job)
        return;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stop_ && !is_worker_thread())
            throw std::runtime_error("SmartParallel ThreadPool is shutting down");
        jobs_.push_back(QueuedJob{std::move(job), nullptr});
        ++active_jobs_;
    }

    condition_.notify_one();
    finished_condition_.notify_all();
}

void ThreadPool::finish_one_job(bool worker_job)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_job && busy_workers_ > 0)
        --busy_workers_;
    if (active_jobs_ > 0)
        --active_jobs_;
    finished_condition_.notify_all();
}

bool ThreadPool::try_execute_one_job()
{
    std::function<void()> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (jobs_.empty())
            return false;
        job = std::move(jobs_.front().function);
        jobs_.pop_front();
    }

    {
        ActivePoolJobScope active_job(this);
        try
        {
            job();
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!unhandled_exception_)
                unhandled_exception_ = std::current_exception();
        }
    }
    finish_one_job(false);
    return true;
}

bool ThreadPool::try_execute_one_dependency_job(const void* dependency)
{
    if (dependency == nullptr)
        return false;

    std::function<void()> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto match = std::find_if(
            jobs_.begin(), jobs_.end(),
            [dependency](const QueuedJob& queued)
            {
                return queued.dependency == dependency;
            });
        if (match == jobs_.end())
            return false;
        job = std::move(match->function);
        jobs_.erase(match);
    }

    try
    {
        ActivePoolJobScope active_job(this);
        job();
    }
    catch (...)
    {
        finish_one_job(false);
        throw;
    }
    finish_one_job(false);
    return true;
}

std::size_t ThreadPool::cancel_dependency_jobs(const void* dependency)
{
    if (dependency == nullptr)
        return 0;

    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t cancelled = 0;
    for (auto it = jobs_.begin(); it != jobs_.end();)
    {
        if (it->dependency == dependency)
        {
            it = jobs_.erase(it);
            ++cancelled;
        }
        else
        {
            ++it;
        }
    }
    active_jobs_ = cancelled > active_jobs_ ? 0 : active_jobs_ - cancelled;
    if (active_jobs_ == 0)
        finished_condition_.notify_all();
    return cancelled;
}

void ThreadPool::submit_dependency_job(std::function<void()> job, const void* dependency)
{
    if (!job)
        return;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stop_ && !is_worker_thread())
            throw std::runtime_error("SmartParallel ThreadPool is shutting down");
        jobs_.push_back(QueuedJob{std::move(job), dependency});
        ++active_jobs_;
    }

    condition_.notify_one();
    finished_condition_.notify_all();
}

void ThreadPool::wait()
{
    const bool reentrant_worker = is_worker_thread();
    // Every cooperatively executed queued job remains counted in active_jobs_
    // until its stack frame returns. Nested wait() therefore has to preserve
    // the complete reentrant stack depth, not assume one active worker frame.
    const std::size_t completion_target = reentrant_worker
        ? std::max<std::size_t>(1, active_worker_job_depth)
        : 0;

    while (true)
    {
        if (reentrant_worker && try_execute_one_job())
            continue;

        std::exception_ptr error;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (active_jobs_ <= completion_target)
            {
                error = unhandled_exception_;
                unhandled_exception_ = nullptr;
            }
            else
            {
                finished_condition_.wait(lock, [this, completion_target]()
                {
                    return active_jobs_ <= completion_target || !jobs_.empty();
                });
                continue;
            }
        }
        if (error)
            std::rethrow_exception(error);
        return;
    }
}

void ThreadPool::execute_visible_work(
    SchedulerVisibleWork& work,
    std::size_t worker_count,
    const std::function<void(const WorkChunk&)>& execute_chunk)
{
    if (!execute_chunk || work.progress().complete())
        return;

    const std::size_t consumers = std::max<std::size_t>(
        1, std::min(worker_count, std::max<std::size_t>(1, thread_count_)));

    if (consumers <= 1)
    {
        for (WorkChunk chunk = work.try_acquire(); chunk.valid(); chunk = work.try_acquire())
        {
            try
            {
                execute_chunk(chunk);
                work.mark_complete(chunk);
            }
            catch (...)
            {
                work.capture_exception(std::current_exception());
                break;
            }
        }
        work.rethrow_if_failed();
        return;
    }

    for (std::size_t consumer = 0; consumer < consumers; ++consumer)
    {
        submit(
            [&work, &execute_chunk]()
            {
                for (WorkChunk chunk = work.try_acquire(); chunk.valid();
                     chunk = work.try_acquire())
                {
                    try
                    {
                        execute_chunk(chunk);
                        work.mark_complete(chunk);
                    }
                    catch (...)
                    {
                        work.capture_exception(std::current_exception());
                        break;
                    }
                }
            });
    }

    wait();
    work.rethrow_if_failed();
}

ThreadPool::CooperativeHelpingResult ThreadPool::execute_visible_work_helping(
    SchedulerVisibleWork& work,
    std::size_t worker_count,
    const std::function<void(const WorkChunk&)>& execute_chunk,
    std::size_t helper_count_limit,
    std::vector<std::shared_ptr<void>> helper_lifetimes)
{
    CooperativeHelpingResult result;
    if (!execute_chunk || work.progress().complete())
        return result;

    const std::size_t consumers = std::max<std::size_t>(
        1, std::min(worker_count, std::max<std::size_t>(1, thread_count_)));
    result.idle_workers_at_submit = idle_worker_count();

    const std::size_t total_chunks = work.total_chunks();
    const std::size_t min_chunks_per_helper = std::max<std::size_t>(
        1, global_config().thread_pool_min_chunks_per_helper);
    const std::size_t useful_helper_limit = total_chunks <= 1
        ? 0
        : (total_chunks - 1) / min_chunks_per_helper;
    std::size_t helper_count = 0;
    if (!helper_lifetimes.empty())
    {
        // The backend already reserved one lease for every lifetime token.
        // Submit exactly that bounded set instead of re-sampling idle workers
        // and accidentally retaining permits for helpers that are never queued.
        helper_count = std::min({
            consumers - 1, helper_count_limit, helper_lifetimes.size(), useful_helper_limit});
    }
    else
    {
        helper_count = std::min(
            recommended_helper_count(work, consumers), helper_count_limit);
    }

    struct CompletionState
    {
        std::size_t remaining = 0;
        std::atomic<std::size_t> started{0};
        std::atomic<std::size_t> useful{0};
        std::mutex mutex;
        std::condition_variable condition;
        std::chrono::steady_clock::time_point remaining_became_zero{};
        std::chrono::steady_clock::time_point all_chunks_completed{};
    };

    const auto completion = std::make_shared<CompletionState>();

    const auto finish_helper = [completion]()
    {
        bool notify = false;
        {
            std::lock_guard<std::mutex> lock(completion->mutex);
            if (completion->remaining > 0)
                --completion->remaining;
            if (completion->remaining == 0)
            {
                completion->remaining_became_zero = std::chrono::steady_clock::now();
                notify = true;
            }
        }
        if (notify)
            completion->condition.notify_all();
    };

    const auto consume = [&work, &execute_chunk, completion]() -> std::size_t
    {
        std::size_t chunks = 0;
        for (WorkChunk chunk = work.try_acquire(); chunk.valid();
             chunk = work.try_acquire())
        {
            try
            {
                execute_chunk(chunk);
                work.mark_complete(chunk);
                if (work.progress().complete())
                {
                    std::lock_guard<std::mutex> lock(completion->mutex);
                    if (completion->all_chunks_completed.time_since_epoch().count() == 0)
                        completion->all_chunks_completed = std::chrono::steady_clock::now();
                }
                ++chunks;
            }
            catch (...)
            {
                work.capture_exception(std::current_exception());
                break;
            }
        }
        return chunks;
    };

    std::size_t submitted_helpers = 0;
    for (std::size_t helper = 0; helper < helper_count; ++helper)
    {
        std::shared_ptr<void> helper_lifetime;
        if (!helper_lifetimes.empty())
            helper_lifetime = std::move(helper_lifetimes[helper]);

        // Register the dependency before publishing the job. A worker may run
        // immediately after submission, so incrementing afterward would allow
        // the completion count to transiently reach zero and lose the final
        // completion transition. If queue publication throws, roll the count
        // back under the same mutex and convert the failure into the region's
        // normal cancellation/exception path.
        {
            std::lock_guard<std::mutex> lock(completion->mutex);
            if (completion->remaining == 0)
                completion->remaining_became_zero = {};
            ++completion->remaining;
        }

        try
        {
            submit_dependency_job(
                [consume,
                 completion,
                 finish_helper,
                 helper_lifetime = std::move(helper_lifetime)]() mutable
                {
                    completion->started.fetch_add(1, std::memory_order_relaxed);
                    if (consume() != 0)
                        completion->useful.fetch_add(1, std::memory_order_relaxed);

                    // A worker thread retains its local std::function until the
                    // next queue iteration. Release the helper permit before
                    // publishing completion; otherwise execute() can return
                    // while the session still appears to have a leased worker.
                    helper_lifetime.reset();
                    finish_helper();
                },
                completion.get());
            ++submitted_helpers;
        }
        catch (...)
        {
            bool notify = false;
            {
                std::lock_guard<std::mutex> lock(completion->mutex);
                if (completion->remaining > 0)
                    --completion->remaining;
                if (completion->remaining == 0)
                {
                    completion->remaining_became_zero = std::chrono::steady_clock::now();
                    notify = true;
                }
            }
            if (notify)
                completion->condition.notify_all();
            work.capture_exception(std::current_exception());
            break;
        }
    }

    result.helper_jobs_submitted = submitted_helpers;

    // The caller is a first-class consumer. This is the cooperative nested
    // boundary: it finishes complete chunks but is never suspended mid-chunk.
    consume();
    const auto useful_work_complete = std::chrono::steady_clock::now();

    if (global_config().thread_pool_cancel_idle_helpers)
    {
        result.helper_jobs_cancelled = cancel_dependency_jobs(completion.get());
        if (result.helper_jobs_cancelled != 0)
        {
            bool notify = false;
            {
                std::lock_guard<std::mutex> lock(completion->mutex);
                completion->remaining = result.helper_jobs_cancelled > completion->remaining
                    ? 0
                    : completion->remaining - result.helper_jobs_cancelled;
                if (completion->remaining == 0)
                {
                    completion->remaining_became_zero = std::chrono::steady_clock::now();
                    notify = true;
                }
            }
            if (notify)
                completion->condition.notify_all();
        }
    }

    std::chrono::steady_clock::time_point wait_started{};
    std::chrono::steady_clock::time_point wait_finished{};
    while (true)
    {
        if (try_execute_one_dependency_job(completion.get()))
        {
            ++result.dependency_jobs_executed_by_waiter;
            continue;
        }

        std::unique_lock<std::mutex> lock(completion->mutex);
        if (completion->remaining == 0)
            break;
        ++result.wait_count;
        wait_started = std::chrono::steady_clock::now();
        completion->condition.wait(lock, [&completion]() { return completion->remaining == 0; });
        wait_finished = std::chrono::steady_clock::now();
        break;
    }

    const auto helpers_retired = std::chrono::steady_clock::now();
    result.work_complete_to_helpers_retired_ms =
        std::chrono::duration<double, std::milli>(helpers_retired - useful_work_complete).count();
    {
        std::lock_guard<std::mutex> lock(completion->mutex);
        const auto all_chunks_completed =
            completion->all_chunks_completed.time_since_epoch().count() != 0
                ? completion->all_chunks_completed
                : helpers_retired;
        if (all_chunks_completed > useful_work_complete)
        {
            result.in_flight_work_drain_ms =
                std::chrono::duration<double, std::milli>(
                    all_chunks_completed - useful_work_complete).count();
        }
        if (helpers_retired > all_chunks_completed)
        {
            result.completion_epilogue_ms =
                std::chrono::duration<double, std::milli>(
                    helpers_retired - all_chunks_completed).count();
        }
        if (result.wait_count != 0 && wait_finished > wait_started)
        {
            result.actual_blocking_wait_ms =
                std::chrono::duration<double, std::milli>(wait_finished - wait_started).count();
            if (completion->remaining_became_zero.time_since_epoch().count() != 0
                && wait_finished >= completion->remaining_became_zero)
            {
                result.completion_signal_to_waiter_wake_ms =
                    std::chrono::duration<double, std::milli>(
                        wait_finished - completion->remaining_became_zero).count();
            }
        }
    }
    result.helper_jobs_started = completion->started.load(std::memory_order_relaxed);
    result.helper_jobs_useful = completion->useful.load(std::memory_order_relaxed);

    work.rethrow_if_failed();
    result.continuation_resumed = true;
    return result;
}

std::size_t ThreadPool::recommended_helper_count(const SchedulerVisibleWork& work,
                                                 std::size_t worker_count) const
{
    const std::size_t consumers = std::max<std::size_t>(
        1, std::min(worker_count, std::max<std::size_t>(1, thread_count_)));
    if (consumers <= 1)
        return 0;

    const std::size_t total_chunks = work.total_chunks();
    const std::size_t min_chunks_per_helper = std::max<std::size_t>(
        1, global_config().thread_pool_min_chunks_per_helper);
    const std::size_t useful_helper_limit = total_chunks <= 1
        ? 0
        : (total_chunks - 1) / min_chunks_per_helper;
    return std::min({consumers - 1, idle_worker_count(), useful_helper_limit});
}

ThreadPool& global_thread_pool()
{
    static ThreadPool pool(hardware_threads());
    return pool;
}
} // namespace smart
