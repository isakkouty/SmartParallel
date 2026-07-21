#include <smart/execution/thread_pool.hpp>
#include <smart/hardware/hardware.hpp>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <utility>

namespace smart
{
namespace
{
thread_local const ThreadPool* active_worker_pool = nullptr;
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
                    }

                    active_worker_pool = this;
                    job();
                    active_worker_pool = nullptr;
                    finish_one_job();
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
        jobs_.push_back(QueuedJob{std::move(job), nullptr});
        ++active_jobs_;
    }

    condition_.notify_one();
}

void ThreadPool::finish_one_job()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_jobs_ > 0)
        --active_jobs_;
    if (active_jobs_ == 0)
        finished_condition_.notify_all();
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

    job();
    finish_one_job();
    return true;
}

void ThreadPool::submit_dependency_job(std::function<void()> job, const void* dependency)
{
    if (!job)
        return;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        jobs_.push_back(QueuedJob{std::move(job), dependency});
        ++active_jobs_;
    }

    condition_.notify_one();
}

void ThreadPool::wait()
{
    std::unique_lock<std::mutex> lock(mutex_);
    finished_condition_.wait(lock,
                             [this]()
                             {
                                 return active_jobs_ == 0;
                             });
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
    const std::function<void(const WorkChunk&)>& execute_chunk)
{
    CooperativeHelpingResult result;
    if (!execute_chunk || work.progress().complete())
        return result;

    const std::size_t consumers = std::max<std::size_t>(
        1, std::min(worker_count, std::max<std::size_t>(1, thread_count_)));
    const std::size_t helper_count = consumers - 1;

    struct CompletionState
    {
        std::atomic<std::size_t> remaining{0};
        std::mutex mutex;
        std::condition_variable condition;
    };

    const auto completion = std::make_shared<CompletionState>();
    completion->remaining.store(helper_count, std::memory_order_relaxed);

    const auto consume = [&work, &execute_chunk]()
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
    };

    for (std::size_t helper = 0; helper < helper_count; ++helper)
    {
        submit_dependency_job(
            [consume, completion]()
            {
                consume();
                completion->remaining.fetch_sub(1, std::memory_order_acq_rel);
                completion->condition.notify_all();
            },
            completion.get());
    }

    result.helper_jobs_submitted = helper_count;

    // The caller is a first-class consumer. This is the cooperative nested
    // boundary: it finishes complete chunks but is never suspended mid-chunk.
    consume();

    while (completion->remaining.load(std::memory_order_acquire) != 0)
    {
        if (try_execute_one_dependency_job(completion.get()))
        {
            ++result.dependency_jobs_executed_by_waiter;
            continue;
        }

        std::unique_lock<std::mutex> lock(completion->mutex);
        completion->condition.wait_for(
            lock,
            std::chrono::milliseconds(1),
            [&completion]()
            {
                return completion->remaining.load(std::memory_order_acquire) == 0;
            });
    }

    work.rethrow_if_failed();
    result.continuation_resumed = true;
    return result;
}

ThreadPool& global_thread_pool()
{
    static ThreadPool pool(hardware_threads());
    return pool;
}
} // namespace smart
