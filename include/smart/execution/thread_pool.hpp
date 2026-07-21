#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <deque>
#include <memory>
#include <limits>
#include <thread>
#include <vector>
#include <smart/execution/work_chunk.hpp>

namespace smart
{
class ThreadPool
{
  public:
    explicit ThreadPool(std::size_t thread_count);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    std::size_t thread_count() const;
    std::size_t idle_worker_count() const;
    bool is_worker_thread() const noexcept;
    void submit(std::function<void()> job);
    void wait();

    // Execute one scheduler-visible region using a bounded number of shared
    // ThreadPool workers. Workers acquire another chunk only after finishing
    // their current chunk, establishing the cooperative scheduling boundary
    // used by later nested-helping and dynamic-lease steps.
    void execute_visible_work(SchedulerVisibleWork& work,
                              std::size_t worker_count,
                              const std::function<void(const WorkChunk&)>& execute_chunk);

    // Execute scheduler-visible work from any thread, including a worker that
    // is already running an outer ThreadPool job. The calling thread consumes
    // chunks directly and, while waiting for submitted helpers to retire, may
    // execute another queued job. This prevents nested waits from deadlocking
    // when every pool worker is currently inside an outer region.
    struct CooperativeHelpingResult
    {
        std::size_t helper_jobs_submitted = 0;
        std::size_t helper_jobs_started = 0;
        std::size_t helper_jobs_useful = 0;
        std::size_t helper_jobs_cancelled = 0;
        std::size_t idle_workers_at_submit = 0;
        double work_complete_to_helpers_retired_ms = 0.0;
        double completion_signal_to_waiter_wake_ms = 0.0;
        std::size_t wait_count = 0;
        std::size_t dependency_jobs_executed_by_waiter = 0;
        std::size_t unrelated_jobs_executed_by_waiter = 0;
        bool continuation_resumed = false;
    };

    CooperativeHelpingResult execute_visible_work_helping(
        SchedulerVisibleWork& work,
        std::size_t worker_count,
        const std::function<void(const WorkChunk&)>& execute_chunk,
        std::size_t helper_count_limit = std::numeric_limits<std::size_t>::max(),
        std::vector<std::shared_ptr<void>> helper_lifetimes = {});

    std::size_t recommended_helper_count(const SchedulerVisibleWork& work,
                                         std::size_t worker_count) const;

  private:
    std::size_t thread_count_;
    std::vector<std::thread> workers_;

    struct QueuedJob
    {
        std::function<void()> function;
        const void* dependency = nullptr;
    };

    std::deque<QueuedJob> jobs_;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::condition_variable finished_condition_;

    bool try_execute_one_dependency_job(const void* dependency);
    std::size_t cancel_dependency_jobs(const void* dependency);
    void submit_dependency_job(std::function<void()> job, const void* dependency);
    void finish_one_job(bool worker_job = false);

    std::size_t active_jobs_ = 0;
    std::size_t busy_workers_ = 0;
    bool stop_ = false;
};

ThreadPool& global_thread_pool();
} // namespace smart
