#pragma once

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

#include <cstddef>
#include <vector>
#include <thread>
#include <functional>

#include <smart/core/config.hpp>
#include <smart/execution/thread_pool.hpp>


namespace smart
{
    class IExecutionEngine
    {
    public:
        virtual ~IExecutionEngine() = default;

        virtual void execute_range(
            std::size_t total,
            std::size_t job_count,
            std::function<void(std::size_t)> func
        ) = 0;
    };

    class ThreadPoolEngine : public IExecutionEngine
    {
    public:
        void execute_range(
            std::size_t total,
            std::size_t job_count,
            std::function<void(std::size_t)> func
        ) override
        {
            ThreadPool& pool = global_thread_pool();

            if (job_count > total)
                job_count = total;

            for (std::size_t t = 0; t < job_count; ++t)
            {
                std::size_t chunk_begin = (total * t) / job_count;
                std::size_t chunk_end = (total * (t + 1)) / job_count;

                pool.submit([chunk_begin, chunk_end, func]()
                {
                    for (std::size_t i = chunk_begin; i < chunk_end; ++i)
                    {
                        func(i);
                    }
                });
            }

            pool.wait();
        }
    };

    class OneTbbEngine : public IExecutionEngine
    {
    public:
        void execute_range(
            std::size_t total,
            std::size_t job_count,
            std::function<void(std::size_t)> func
        ) override
        {
            (void)job_count;

            tbb::parallel_for(
                tbb::blocked_range<std::size_t>(0, total),
                [&](const tbb::blocked_range<std::size_t>& range)
                {
                    for (std::size_t i = range.begin(); i < range.end(); ++i)
                    {
                        func(i);
                    }
                }
            );
        }
    };

    class StaticThreadEngine : public IExecutionEngine
    {
    public:
        void execute_range(
            std::size_t total,
            std::size_t job_count,
            std::function<void(std::size_t)> func
        ) override
        {
            if (job_count > total)
                job_count = total;

            std::vector<std::thread> threads;

            for (std::size_t t = 0; t < job_count; ++t)
            {
                std::size_t begin = (total * t) / job_count;
                std::size_t end = (total * (t + 1)) / job_count;

                threads.emplace_back([begin, end, func]()
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
        {
            return static_thread_engine;
        }
        
        if (type == ExecutionEngineType::OneTbb)
        {
            return one_tbb_engine;
        }

        return thread_pool_engine;
    }

    inline IExecutionEngine& default_execution_engine()
    {
        return execution_engine(global_config().execution_engine);
    }
}
