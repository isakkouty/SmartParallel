#include <smart/decision/execution_plan.hpp>
#include <smart/execution/executor.hpp>
#include <smart/workload/workload.hpp>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <vector>

namespace
{
    void verify_plan(const smart::ExecutionPlan& plan)
    {
        constexpr std::size_t total = 10'003;
        smart::Workload workload;
        workload.iterations = total;

        std::vector<std::atomic<unsigned int>> visits(total);
        for (auto& value : visits)
            value.store(0, std::memory_order_relaxed);

        const smart::ExecutionStats stats = smart::execute_workload(
            workload,
            plan,
            [&](std::size_t index)
            {
                visits[index].fetch_add(1, std::memory_order_relaxed);
            });

        assert(stats.iterations == total);
        for (const auto& value : visits)
            assert(value.load(std::memory_order_relaxed) == 1);
    }
}

int main()
{
    smart::ExecutionPlan thread_pool;
    thread_pool.parallel = true;
    thread_pool.engine = smart::ExecutionEngineType::ThreadPool;
    thread_pool.strategy = smart::ExecutionStrategy::DynamicChunks;
    thread_pool.job_count = 2;
    thread_pool.chunk_size = 7;
    verify_plan(thread_pool);

    smart::ExecutionPlan one_tbb = thread_pool;
    one_tbb.engine = smart::ExecutionEngineType::OneTbb;
    one_tbb.job_count = 2;
    one_tbb.chunk_size = 13;
    verify_plan(one_tbb);

    smart::ExecutionPlan static_threads;
    static_threads.parallel = true;
    static_threads.engine = smart::ExecutionEngineType::StaticThread;
    static_threads.strategy = smart::ExecutionStrategy::StaticChunks;
    static_threads.job_count = 2;
    static_threads.chunk_size = 0;
    verify_plan(static_threads);

    return 0;
}
