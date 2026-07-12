#pragma once

#include <cstddef>
#include <thread>
#include <vector>
#include <mutex>
#include <queue>
#include <functional>
#include <condition_variable>

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
        void submit(std::function<void()> job);
        void wait();

    private:
        std::size_t thread_count_;
        std::vector<std::thread> workers_;

        std::queue<std::function<void()>> jobs_;

        std::mutex mutex_;
        std::condition_variable condition_;
        std::condition_variable finished_condition_;

        std::size_t active_jobs_ = 0;
        bool stop_ = false;
    };

    ThreadPool& global_thread_pool();
}
