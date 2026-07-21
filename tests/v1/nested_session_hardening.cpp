#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include <smart/execution/backend.hpp>
#include <smart/execution/parallel.hpp>
#include <smart/execution/thread_pool.hpp>

namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

struct ConfigGuard
{
    smart::Config saved = smart::global_config();
    ~ConfigGuard() { smart::global_config() = saved; }
};

smart::BackendExecutionResult execute_direct(
    const std::shared_ptr<smart::NestedExecutionSession>& session,
    std::size_t total,
    std::size_t budget,
    std::function<void(std::size_t)> function)
{
    smart::BackendExecutionRequest request;
    request.total = total;
    request.concurrency_budget = budget;
    request.chunk_size = 1;
    request.nested_session = session;
    request.function = std::move(function);
    return smart::execution_backend(smart::ExecutionEngineType::ThreadPool)
        .execute(std::move(request));
}

void test_single_iteration_uses_one_lease()
{
    auto session = std::make_shared<smart::NestedExecutionSession>(4, 1);
    std::atomic<std::size_t> observed_leases{0};
    execute_direct(session, 1, 4, [&](std::size_t)
    {
        require(session->current_thread_owns_participant(),
                "callback did not own a session participant");
        observed_leases.store(session->leased_workers(), std::memory_order_relaxed);
    });

    require(observed_leases.load(std::memory_order_relaxed) == 1,
            "single-iteration execution reserved phantom worker leases");
    require(session->maximum_leased_workers() == 1,
            "single-iteration execution exceeded one participant");
    require(session->leased_workers() == 0, "caller lease leaked after execution");
    require(session->lease_invariant_violations() == 0,
            "lease invariant violation was recorded");
}

void test_new_root_from_pool_worker_counts_caller()
{
    std::promise<void> completed;
    auto future = completed.get_future();
    std::exception_ptr failure;
    std::mutex failure_mutex;

    smart::global_thread_pool().submit([&]
    {
        try
        {
            auto session = std::make_shared<smart::NestedExecutionSession>(4, 2);
            execute_direct(session, 1, 4, [&](std::size_t)
            {
                require(session->current_thread_owns_participant(),
                        "reentrant root callback did not own its session participant");
                require(session->leased_workers() == 1,
                        "pool-worker root inferred a nonexistent inherited lease");
            });
            require(session->maximum_leased_workers() == 1,
                    "pool-worker root reserved phantom helper leases");
            require(session->leased_workers() == 0,
                    "pool-worker root leaked its caller lease");
            require(session->lease_invariant_violations() == 0,
                    "pool-worker root violated lease invariants");
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(failure_mutex);
            failure = std::current_exception();
        }
        completed.set_value();
    });

    require(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
            "pool-worker root did not complete");
    if (failure)
        std::rethrow_exception(failure);
}

void test_helper_completion_and_exception_release()
{
    auto session = std::make_shared<smart::NestedExecutionSession>(4, 3);
    bool threw = false;
    try
    {
        execute_direct(session, 256, 4, [](std::size_t i)
        {
            if (i == 7)
                throw std::runtime_error("expected helper failure");
            volatile std::size_t value = i;
            for (std::size_t round = 0; round < 50; ++round)
                value = value * 1664525u + 1013904223u;
            (void)value;
        });
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }

    require(threw, "helper callback exception was swallowed");
    require(session->leased_workers() == 0,
            "helper or caller lease leaked after cancellation");
    require(session->maximum_leased_workers() <= 4,
            "helper execution exceeded the root budget");
    require(session->lease_invariant_violations() == 0,
            "exception path violated lease invariants");

    std::atomic<std::size_t> visits{0};
    execute_direct(session, 128, 4, [&](std::size_t)
    {
        visits.fetch_add(1, std::memory_order_relaxed);
    });
    require(visits.load(std::memory_order_relaxed) == 128,
            "scheduler did not recover after helper cancellation");
    require(session->leased_workers() == 0,
            "lease leaked after post-exception recovery");
}


void test_helper_permits_release_before_return()
{
    auto session = std::make_shared<smart::NestedExecutionSession>(4, 30);
    for (std::size_t repetition = 0; repetition < 1000; ++repetition)
    {
        std::atomic<std::size_t> visits{0};
        execute_direct(session, 16, 4, [&](std::size_t)
        {
            visits.fetch_add(1, std::memory_order_relaxed);
        });
        require(visits.load(std::memory_order_relaxed) == 16,
                "helper permit stress lost work");
        require(session->leased_workers() == 0,
                "execute returned before helper permits were released");
        require(session->lease_invariant_violations() == 0,
                "helper permit stress violated lease invariants");
    }
}

void test_completion_protocol_stress()
{
    smart::ThreadPool pool(4);
    for (std::size_t repetition = 0; repetition < 2000; ++repetition)
    {
        smart::SchedulerVisibleWork work(0, 4, 1);
        std::atomic<std::size_t> visits{0};
        const auto result = pool.execute_visible_work_helping(
            work,
            4,
            [&](const smart::WorkChunk&)
            {
                visits.fetch_add(1, std::memory_order_relaxed);
            });
        require(visits.load(std::memory_order_relaxed) == 4,
                "completion stress skipped a chunk");
        require(work.progress().complete(), "completion stress returned before work completed");
        require(result.completion_signal_to_waiter_wake_ms >= 0.0,
                "invalid completion wake measurement");
    }
}

void test_collision_safe_plan_snapshots()
{
    smart::NestedExecutionSession session(4, 4);
    smart::NestedPlanSnapshotKey first{11, 8, 64, 3, 19, 4,
                                       smart::ExecutionEngineType::ThreadPool};
    smart::NestedPlanSnapshotKey second = first;
    second.depth = 4;

    smart::ExecutionPlan first_plan;
    first_plan.parallel = true;
    first_plan.job_count = 4;
    smart::ExecutionPlan second_plan;
    second_plan.parallel = false;
    second_plan.job_count = 1;

    session.store_plan_snapshot(first, first_plan);
    session.store_plan_snapshot(second, second_plan);
    const auto found_first = session.find_plan_snapshot(first);
    const auto found_second = session.find_plan_snapshot(second);
    require(found_first && found_first->parallel && found_first->job_count == 4,
            "first full snapshot key was not preserved");
    require(found_second && !found_second->parallel && found_second->job_count == 1,
            "second full snapshot key aliased the first");
}

void test_multiple_roots_keep_independent_invariants()
{
    constexpr std::size_t root_count = 4;
    std::vector<std::future<void>> roots;
    roots.reserve(root_count);
    for (std::size_t root = 0; root < root_count; ++root)
    {
        roots.push_back(std::async(std::launch::async, [root]
        {
            auto session = std::make_shared<smart::NestedExecutionSession>(2, 100 + root);
            std::atomic<std::size_t> visits{0};
            execute_direct(session, 64, 2, [&](std::size_t)
            {
                visits.fetch_add(1, std::memory_order_relaxed);
            });
            require(visits.load(std::memory_order_relaxed) == 64,
                    "concurrent root lost work");
            require(session->maximum_leased_workers() <= 2,
                    "concurrent root exceeded its per-root budget");
            require(session->leased_workers() == 0,
                    "concurrent root leaked leases");
            require(session->lease_invariant_violations() == 0,
                    "concurrent root violated lease invariants");
        }));
    }
    for (auto& root : roots)
        root.get();
}
} // namespace

int main()
{
    try
    {
        ConfigGuard guard;
        auto& config = smart::global_config();
        config.enable_experience = false;
        config.execution_engine = smart::ExecutionEngineType::ThreadPool;
        config.enable_nested_execution_session = true;
        config.thread_pool_cancel_idle_helpers = true;

        test_single_iteration_uses_one_lease();
        test_new_root_from_pool_worker_counts_caller();
        test_helper_completion_and_exception_release();
        test_helper_permits_release_before_return();
        test_completion_protocol_stress();
        test_collision_safe_plan_snapshots();
        test_multiple_roots_keep_independent_invariants();
        std::cout << "nested session hardening: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "nested session hardening: FAIL: " << error.what() << '\n';
        return 1;
    }
}
