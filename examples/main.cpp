#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <smart/core/config.hpp>
#include <smart/execution/backend.hpp>
#include <smart/execution/execution_context.hpp>
#include <smart/execution/nested_budget_partition.hpp>
#include <smart/execution/nested_execution_coordinator.hpp>
#include <smart/execution/work_chunk.hpp>
#include <smart/execution/thread_pool.hpp>
#include <smart/execution/parallel.hpp>

namespace
{
bool verify_runtime(smart::ExecutionEngineType expected_type,
                    const char* expected_name,
                    const smart::RuntimeCapabilities& expected_capabilities)
{
    const smart::IExecutionEngine& runtime = smart::execution_engine(expected_type);
    const smart::RuntimeCapabilities actual = runtime.capabilities();

    const bool passed = runtime.type() == expected_type
                        && std::string(runtime.name()) == expected_name
                        && actual.supports_native_nesting
                               == expected_capabilities.supports_native_nesting
                        && actual.uses_shared_workers == expected_capabilities.uses_shared_workers
                        && actual.supports_concurrency_limit
                               == expected_capabilities.supports_concurrency_limit
                        && actual.supports_dynamic_chunks
                               == expected_capabilities.supports_dynamic_chunks;

    std::cout << "Runtime " << runtime.name() << ": " << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
}

smart::ExecutionPlan parallel_plan(smart::ExecutionEngineType engine)
{
    smart::ExecutionPlan plan;
    plan.parallel = true;
    plan.strategy = smart::ExecutionStrategy::DynamicChunks;
    plan.engine = engine;
    plan.job_count = 4;
    plan.chunk_size = 1;
    return plan;
}

bool verify_coordinator_decision(const char* label,
                                 const smart::ExecutionContext& parent,
                                 const smart::ExecutionPlan& requested_plan,
                                 smart::NestedExecutionPolicy expected_policy,
                                 bool expected_parallel,
                                 std::size_t expected_budget,
                                 const smart::NestedBudgetPartition* partition = nullptr,
                                 std::size_t expected_allocation = 0)
{
    const smart::NestedExecutionDecision decision = partition == nullptr
        ? smart::NestedExecutionCoordinator{}.coordinate(parent, requested_plan)
        : smart::NestedExecutionCoordinator{}.coordinate(parent, requested_plan, *partition);
    const smart::ExecutionPlan& effective_plan = decision.plan;
    const bool expected_fallback =
        expected_policy == smart::NestedExecutionPolicy::SequentialFallback;
    const bool expected_limited =
        expected_policy == smart::NestedExecutionPolicy::BudgetLimitedDelegation;
    const bool passed = decision.policy == expected_policy
                        && effective_plan.parallel == expected_parallel
                        && decision.effective_budget == expected_budget
                        && effective_plan.job_count == expected_budget
                        && decision.uses_sequential_fallback() == expected_fallback
                        && decision.uses_budget_limited_delegation() == expected_limited
                        && (partition == nullptr || decision.allocated_budget == expected_allocation)
                        && (expected_parallel
                                || (effective_plan.strategy == smart::ExecutionStrategy::Sequential
                                    && effective_plan.job_count == 1
                                    && effective_plan.chunk_size == 0));

    std::cout << "Coordinator " << label << ": " << (passed ? "PASS" : "FAIL")
              << " [" << smart::nested_execution_policy_name(decision.policy)
              << ", budget=" << decision.effective_budget;
    if (partition != nullptr)
        std::cout << ", allocation=" << decision.allocated_budget << ", sibling="
                  << decision.sibling_index << "/" << decision.sibling_count;
    std::cout << "]\n";
    return passed;
}
} // namespace

int main()
{
    smart::global_config().enable_experience = false;
    smart::global_config().enable_parallel_for_auto_profiling = false;
    smart::global_config().enable_parallel_for_profile_cache = false;

    constexpr std::size_t outer_iterations = 4;
    constexpr std::size_t inner_iterations = 3;

    std::atomic<std::size_t> outer_context_checks{0};
    std::atomic<std::size_t> nested_context_checks{0};
    std::atomic<bool> failed{false};

    smart::parallel_for(
        0,
        outer_iterations,
        [&](std::size_t)
        {
            const smart::ExecutionContext outer = smart::current_execution_context();
            if (outer.loop_id == 0 || outer.parent_loop_id != 0 || outer.depth != 1
                || outer.nested() || outer.concurrency_budget < 1)
            {
                failed.store(true, std::memory_order_relaxed);
            }
            else
            {
                outer_context_checks.fetch_add(1, std::memory_order_relaxed);
            }

            smart::parallel_for(
                0,
                inner_iterations,
                [&](std::size_t)
                {
                    const smart::ExecutionContext inner = smart::current_execution_context();
                    if (inner.loop_id == 0 || inner.loop_id == outer.loop_id
                        || inner.parent_loop_id != outer.loop_id || inner.depth != 2
                        || !inner.nested() || inner.concurrency_budget != 1
                        || inner.concurrency_budget > outer.concurrency_budget)
                    {
                        failed.store(true, std::memory_order_relaxed);
                    }
                    else
                    {
                        nested_context_checks.fetch_add(1, std::memory_order_relaxed);
                    }
                });
        });

    const std::size_t expected_nested_checks = outer_iterations * inner_iterations;
    const bool context_passed = !failed.load(std::memory_order_relaxed)
                                && outer_context_checks.load(std::memory_order_relaxed)
                                       == outer_iterations
                                && nested_context_checks.load(std::memory_order_relaxed)
                                       == expected_nested_checks
                                && !smart::inside_parallel_loop();

    std::cout << "Outer context checks: " << outer_context_checks << '/' << outer_iterations
              << '\n';
    std::cout << "Nested context checks: " << nested_context_checks << '/'
              << expected_nested_checks << '\n';
    std::cout << (context_passed ? "PASS: nested execution context detected correctly.\n"
                                : "FAIL: nested execution context is incorrect.\n");

    const bool thread_pool_passed =
        verify_runtime(smart::ExecutionEngineType::ThreadPool,
                       "thread_pool",
                       smart::RuntimeCapabilities{false, true, true, true});
    const bool one_tbb_passed = verify_runtime(smart::ExecutionEngineType::OneTbb,
                                               "one_tbb",
                                               smart::RuntimeCapabilities{true, true, true, true});
    const bool static_thread_passed =
        verify_runtime(smart::ExecutionEngineType::StaticThread,
                       "static_thread",
                       smart::RuntimeCapabilities{false, false, true, false});

    const smart::IExecutionEngine& automatic =
        smart::execution_engine(smart::ExecutionEngineType::Auto);
    const bool auto_passed = automatic.type() == smart::ExecutionEngineType::ThreadPool;
    std::cout << "Runtime auto resolution: " << (auto_passed ? "PASS" : "FAIL") << '\n';

    smart::ExecutionContext tbb_parent;
    tbb_parent.loop_id = 1;
    tbb_parent.depth = 1;
    tbb_parent.engine = smart::ExecutionEngineType::OneTbb;
    tbb_parent.parallel = true;
    tbb_parent.concurrency_budget = 3;

    smart::ExecutionContext pool_parent = tbb_parent;
    pool_parent.engine = smart::ExecutionEngineType::ThreadPool;

    smart::ExecutionContext sequential_parent = tbb_parent;
    sequential_parent.parallel = false;

    smart::ExecutionPlan fitting_tbb_plan = parallel_plan(smart::ExecutionEngineType::OneTbb);
    fitting_tbb_plan.job_count = 2;
    const bool tbb_native = verify_coordinator_decision(
        "oneTBB fitting budget 2 <= 3",
        tbb_parent,
        fitting_tbb_plan,
        smart::NestedExecutionPolicy::NativeRuntimeDelegation,
        true,
        2);

    smart::ExecutionPlan oversized_tbb_plan = parallel_plan(smart::ExecutionEngineType::OneTbb);
    oversized_tbb_plan.job_count = 8;
    const bool limited_tbb = verify_coordinator_decision(
        "oneTBB reduced budget 3 <- 8",
        tbb_parent,
        oversized_tbb_plan,
        smart::NestedExecutionPolicy::BudgetLimitedDelegation,
        true,
        3);

    smart::ExecutionContext exhausted_tbb_parent = tbb_parent;
    exhausted_tbb_parent.concurrency_budget = 1;
    const bool exhausted_tbb = verify_coordinator_decision(
        "oneTBB exhausted budget 1",
        exhausted_tbb_parent,
        fitting_tbb_plan,
        smart::NestedExecutionPolicy::SequentialFallback,
        false,
        1);

    const bool pool_fallback = verify_coordinator_decision(
        "ThreadPool -> ThreadPool",
        pool_parent,
        parallel_plan(smart::ExecutionEngineType::ThreadPool),
        smart::NestedExecutionPolicy::SequentialFallback,
        false,
        1);
    const bool cross_runtime_fallback = verify_coordinator_decision(
        "oneTBB -> ThreadPool",
        tbb_parent,
        parallel_plan(smart::ExecutionEngineType::ThreadPool),
        smart::NestedExecutionPolicy::SequentialFallback,
        false,
        1);
    const bool inactive_parent = verify_coordinator_decision(
        "sequential parent -> ThreadPool",
        sequential_parent,
        parallel_plan(smart::ExecutionEngineType::ThreadPool),
        smart::NestedExecutionPolicy::NotNested,
        true,
        4);


    const smart::NestedBudgetPartitioner partitioner;
    const smart::NestedBudgetPartition split_0 = partitioner.partition(8, 3, 0);
    const smart::NestedBudgetPartition split_1 = partitioner.partition(8, 3, 1);
    const smart::NestedBudgetPartition split_2 = partitioner.partition(8, 3, 2);
    const bool fair_partition = split_0.allocated_budget == 3
                                && split_1.allocated_budget == 3
                                && split_2.allocated_budget == 2;
    std::cout << "Partition budget 8 across 3 children: "
              << (fair_partition ? "PASS" : "FAIL") << " [3,3,2]\n";

    const smart::NestedBudgetPartition exhausted_0 = partitioner.partition(2, 4, 0);
    const smart::NestedBudgetPartition exhausted_1 = partitioner.partition(2, 4, 1);
    const smart::NestedBudgetPartition exhausted_2 = partitioner.partition(2, 4, 2);
    const smart::NestedBudgetPartition exhausted_3 = partitioner.partition(2, 4, 3);
    const bool exhausted_partition = exhausted_0.allocated_budget == 1
                                     && exhausted_1.allocated_budget == 1
                                     && exhausted_2.allocated_budget == 0
                                     && exhausted_3.allocated_budget == 0
                                     && exhausted_2.exhausted() && exhausted_3.exhausted();
    std::cout << "Partition budget 2 across 4 children: "
              << (exhausted_partition ? "PASS" : "FAIL") << " [1,1,0,0]\n";

    smart::ExecutionContext partition_parent = tbb_parent;
    partition_parent.concurrency_budget = 8;
    smart::ExecutionPlan partitioned_plan = parallel_plan(smart::ExecutionEngineType::OneTbb);
    partitioned_plan.job_count = 6;
    const bool partition_limited = verify_coordinator_decision(
        "oneTBB sibling 0/3 receives 3 of 8",
        partition_parent,
        partitioned_plan,
        smart::NestedExecutionPolicy::BudgetLimitedDelegation,
        true,
        3,
        &split_0,
        3);

    const bool partition_exhausted_fallback = verify_coordinator_decision(
        "oneTBB sibling 2/4 receives 0 of 2",
        exhausted_tbb_parent,
        fitting_tbb_plan,
        smart::NestedExecutionPolicy::SequentialFallback,
        false,
        1,
        &exhausted_2,
        0);

    smart::ExecutionPlan zero_budget_plan = parallel_plan(smart::ExecutionEngineType::ThreadPool);
    zero_budget_plan.job_count = 0;
    const bool clamped_root_budget = verify_coordinator_decision(
        "root budget clamp 0 -> 1",
        smart::ExecutionContext{},
        zero_budget_plan,
        smart::NestedExecutionPolicy::NotNested,
        true,
        1);

    smart::SchedulerVisibleWork visible_work(5, 28, 6);
    const smart::WorkChunk chunk_0 = visible_work.try_acquire();
    const smart::WorkChunk chunk_1 = visible_work.try_acquire();
    const smart::WorkChunk chunk_2 = visible_work.try_acquire();
    const smart::WorkChunk chunk_3 = visible_work.try_acquire();
    const smart::WorkChunk no_chunk = visible_work.try_acquire();
    visible_work.mark_complete(chunk_0);
    visible_work.mark_complete(chunk_1);
    visible_work.mark_complete(chunk_2);
    visible_work.mark_complete(chunk_3);
    const smart::WorkChunkProgress visible_progress = visible_work.progress();
    const bool deterministic_chunks = chunk_0.begin == 5 && chunk_0.end == 11
                                      && chunk_0.ordinal == 0
                                      && chunk_1.begin == 11 && chunk_1.end == 17
                                      && chunk_1.ordinal == 1
                                      && chunk_2.begin == 17 && chunk_2.end == 23
                                      && chunk_2.ordinal == 2
                                      && chunk_3.begin == 23 && chunk_3.end == 28
                                      && chunk_3.ordinal == 3 && !no_chunk.valid()
                                      && visible_progress.total_iterations == 23
                                      && visible_progress.total_chunks == 4
                                      && visible_progress.acquired_chunks == 4
                                      && visible_progress.completed_chunks == 4
                                      && visible_progress.complete();
    std::cout << "Scheduler-visible chunks [5,28), size 6: "
              << (deterministic_chunks ? "PASS" : "FAIL")
              << " [[5,11),[11,17),[17,23),[23,28)]\n";

    std::atomic<bool> nested_visible_passed{true};
    smart::parallel_for(
        0,
        std::size_t{1},
        [&](std::size_t)
        {
            const smart::ExecutionContext owner = smart::current_execution_context();
            smart::SchedulerVisibleWork nested_work(0, 10, 4);
            const bool owner_ok = owner.loop_id != 0
                                  && nested_work.owner_loop_id() == owner.loop_id
                                  && nested_work.parent_loop_id() == owner.parent_loop_id
                                  && nested_work.owner_depth() == owner.depth;
            std::size_t processed = 0;
            for (smart::WorkChunk chunk = nested_work.try_acquire(); chunk.valid();
                 chunk = nested_work.try_acquire())
            {
                processed += chunk.size();
                nested_work.mark_complete(chunk);
            }
            const smart::WorkChunkProgress progress = nested_work.progress();
            if (!owner_ok || processed != 10 || progress.total_chunks != 3
                || progress.completed_chunks != 3 || !progress.complete())
            {
                nested_visible_passed.store(false, std::memory_order_relaxed);
            }
        });
    std::cout << "Scheduler-visible owner context propagation: "
              << (nested_visible_passed.load(std::memory_order_relaxed) ? "PASS" : "FAIL")
              << '\n';

    smart::ThreadPool shared_pool(4);
    smart::SchedulerVisibleWork shared_work(0, 128, 5);
    std::vector<std::atomic<unsigned int>> visits(128);
    for (auto& visit : visits)
        visit.store(0, std::memory_order_relaxed);
    std::mutex worker_ids_mutex;
    std::set<std::thread::id> worker_ids;

    shared_pool.execute_visible_work(
        shared_work,
        4,
        [&](const smart::WorkChunk& chunk)
        {
            {
                std::lock_guard<std::mutex> lock(worker_ids_mutex);
                worker_ids.insert(std::this_thread::get_id());
            }
            for (std::size_t i = chunk.begin; i < chunk.end; ++i)
                visits[i].fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });

    bool every_iteration_once = true;
    for (const auto& visit : visits)
        every_iteration_once = every_iteration_once
                               && visit.load(std::memory_order_relaxed) == 1;
    const smart::WorkChunkProgress shared_progress = shared_work.progress();
    const bool shared_queue_passed = every_iteration_once
                                     && shared_progress.total_chunks == 26
                                     && shared_progress.acquired_chunks == 26
                                     && shared_progress.completed_chunks == 26
                                     && shared_progress.complete()
                                     && worker_ids.size() >= 2
                                     && worker_ids.size() <= 4;
    std::cout << "Shared ThreadPool consumers process each chunk once: "
              << (shared_queue_passed ? "PASS" : "FAIL")
              << " [chunks=" << shared_progress.completed_chunks
              << ", workers=" << worker_ids.size() << "]\n";

    smart::ThreadPool nested_help_pool(4);
    smart::SchedulerVisibleWork nested_help_work(0, 96, 4);
    std::vector<std::atomic<unsigned int>> nested_help_visits(96);
    for (auto& visit : nested_help_visits)
        visit.store(0, std::memory_order_relaxed);
    std::mutex nested_help_ids_mutex;
    std::set<std::thread::id> nested_help_ids;
    std::atomic<bool> nested_help_outer_ran{false};

    nested_help_pool.submit(
        [&]()
        {
            nested_help_outer_ran.store(true, std::memory_order_relaxed);
            nested_help_pool.execute_visible_work_helping(
                nested_help_work,
                4,
                [&](const smart::WorkChunk& chunk)
                {
                    {
                        std::lock_guard<std::mutex> lock(nested_help_ids_mutex);
                        nested_help_ids.insert(std::this_thread::get_id());
                    }
                    for (std::size_t i = chunk.begin; i < chunk.end; ++i)
                        nested_help_visits[i].fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                });
        });
    nested_help_pool.wait();

    bool nested_help_once = true;
    for (const auto& visit : nested_help_visits)
        nested_help_once = nested_help_once
                           && visit.load(std::memory_order_relaxed) == 1;
    const smart::WorkChunkProgress nested_help_progress = nested_help_work.progress();
    const bool nested_help_passed = nested_help_outer_ran.load(std::memory_order_relaxed)
                                    && nested_help_once
                                    && nested_help_progress.total_chunks == 24
                                    && nested_help_progress.completed_chunks == 24
                                    && nested_help_progress.complete()
                                    && nested_help_ids.size() >= 2
                                    && nested_help_ids.size() <= 4;
    std::cout << "Nested ThreadPool worker helps inner visible work: "
              << (nested_help_passed ? "PASS" : "FAIL")
              << " [chunks=" << nested_help_progress.completed_chunks
              << ", workers=" << nested_help_ids.size() << "]\n";

    smart::ThreadPool saturated_pool(4);
    constexpr std::size_t saturated_outer_count = 4;
    constexpr std::size_t saturated_inner_count = 48;
    std::vector<std::atomic<unsigned int>> saturated_visits(
        saturated_outer_count * saturated_inner_count);
    for (auto& visit : saturated_visits)
        visit.store(0, std::memory_order_relaxed);
    std::atomic<std::size_t> saturated_completed_regions{0};

    for (std::size_t outer = 0; outer < saturated_outer_count; ++outer)
    {
        saturated_pool.submit(
            [&, outer]()
            {
                smart::SchedulerVisibleWork inner_work(0, saturated_inner_count, 4);
                saturated_pool.execute_visible_work_helping(
                    inner_work,
                    4,
                    [&, outer](const smart::WorkChunk& chunk)
                    {
                        for (std::size_t i = chunk.begin; i < chunk.end; ++i)
                        {
                            saturated_visits[outer * saturated_inner_count + i]
                                .fetch_add(1, std::memory_order_relaxed);
                        }
                    });
                if (inner_work.progress().complete())
                    saturated_completed_regions.fetch_add(1, std::memory_order_relaxed);
            });
    }
    saturated_pool.wait();

    bool saturated_once = true;
    for (const auto& visit : saturated_visits)
        saturated_once = saturated_once
                         && visit.load(std::memory_order_relaxed) == 1;
    const bool saturated_help_passed = saturated_once
                                       && saturated_completed_regions.load(
                                              std::memory_order_relaxed)
                                              == saturated_outer_count;
    std::cout << "Saturated nested ThreadPool waits remain deadlock-free: "
              << (saturated_help_passed ? "PASS" : "FAIL")
              << " [regions="
              << saturated_completed_regions.load(std::memory_order_relaxed)
              << "/" << saturated_outer_count << "]\n";

    const bool passed = context_passed && thread_pool_passed && one_tbb_passed
                        && static_thread_passed && auto_passed && tbb_native && limited_tbb
                        && exhausted_tbb && pool_fallback && cross_runtime_fallback
                        && inactive_parent && fair_partition && exhausted_partition
                        && partition_limited && partition_exhausted_fallback
                        && clamped_root_budget && deterministic_chunks
                        && nested_visible_passed.load(std::memory_order_relaxed)
                        && shared_queue_passed && nested_help_passed
                        && saturated_help_passed;

    std::cout << (passed ? "PASS: nested ThreadPool helping is correct.\n"
                         : "FAIL: nested ThreadPool helping mismatch.\n");
    return passed ? 0 : 1;
}
