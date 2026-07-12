#include <smart/execution/thread_pool.hpp>
#include <smart/hardware/hardware.hpp>

#include <utility>

namespace smart
{
ThreadPool::ThreadPool(std::size_t thread_count)
    : thread_count_(thread_count)
    {
        for (std::size_t i = 0; i < thread_count_; ++i)
        {
            workers_.emplace_back([this]()
            {
                while (true)
                {
                    std::unique_lock<std::mutex> lock(mutex_);

                    condition_.wait(lock, [this]()
                    {
                        return stop_ || !jobs_.empty();
                    });

                    if (stop_ && jobs_.empty())
                        return;

                    std::function<void()> job = std::move(jobs_.front());
                    jobs_.pop();

                    lock.unlock();

                    job();

                    lock.lock();
                    --active_jobs_;

                    if (active_jobs_ == 0)
                    {
                        finished_condition_.notify_all();
                    }
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
            {
                worker.join();
            }
        }
    }

    std::size_t ThreadPool::thread_count() const
    {
        return thread_count_;
    }

    void ThreadPool::submit(std::function<void()> job)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            jobs_.push(std::move(job));
            ++active_jobs_;
        }

        condition_.notify_one();
    }

    void ThreadPool::wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        finished_condition_.wait(lock, [this]()
        {
            return active_jobs_ == 0;
        });
    }

    ThreadPool& global_thread_pool()
    {
        static ThreadPool pool(hardware_threads());
        return pool;
    }
}
