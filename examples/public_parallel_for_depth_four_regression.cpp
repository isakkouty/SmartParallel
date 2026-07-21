#include <smart/execution/parallel.hpp>
#include <smart/execution/thread_pool.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
struct ConfigGuard
{
    smart::Config saved = smart::global_config();
    ~ConfigGuard() { smart::global_config() = saved; }
};

void configure_regression_path()
{
    auto& config = smart::global_config();
    config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    config.enable_experience = false;
    config.enable_machine_runtime_calibration = false;
    config.enable_parallel_for_profile_cache = false;
    config.enable_parallel_for_cached_sequential_fast_path = false;
    config.enable_parallel_for_auto_profiling = true;
    config.parallel_for_profile_min_samples = 1;
    config.parallel_for_profile_max_samples = 2;
    config.parallel_for_profile_regions = 2;
    config.parallel_for_profile_min_signal_ms = 0.0;
    config.parallel_for_estimated_overhead_ms = 0.000001;
    config.parallel_for_minimum_predicted_speedup = 1.0;
    config.enable_nested_granularity_enforcement = true;
    config.nested_min_iterations_per_worker = 1;
    config.nested_min_chunks_per_worker = 1;
    config.nested_target_chunks_per_worker = 2;
}

smart::ExecutionContext bounded_thread_pool_parent(std::uint64_t loop_id)
{
    smart::ExecutionContext parent;
    parent.loop_id = loop_id;
    parent.depth = 1;
    parent.engine = smart::ExecutionEngineType::ThreadPool;
    parent.parallel = true;
    parent.nested_policy = smart::NestedExecutionPolicy::CooperativeHelping;
    parent.concurrency_budget = 4;
    parent.root_loop_id = loop_id;
    parent.nearest_parallel_ancestor_loop_id = loop_id;
    parent.runtime_owner_loop_id = loop_id;
    parent.root_engine = smart::ExecutionEngineType::ThreadPool;
    parent.nearest_parallel_ancestor_engine = smart::ExecutionEngineType::ThreadPool;
    parent.runtime_owner_engine = smart::ExecutionEngineType::ThreadPool;
    parent.inherited_concurrency_budget = 4;
    return parent;
}

void run_depth(std::size_t depth,
               std::size_t prefix,
               std::vector<std::atomic<unsigned>>& leaves,
               std::atomic<std::size_t>& callbacks_on_pool_workers)
{
    constexpr std::size_t fanout = 4;
    smart::parallel_for(0, fanout, [&](std::size_t index) {
        if (smart::global_thread_pool().is_worker_thread())
            callbacks_on_pool_workers.fetch_add(1, std::memory_order_relaxed);

        const std::size_t next = prefix * fanout + index;
        if (depth == 1)
        {
            leaves[next].fetch_add(1, std::memory_order_relaxed);
            // Keep the sampled callback expensive enough that the public
            // decision path remains eligible for ThreadPool execution.
            std::atomic_signal_fence(std::memory_order_seq_cst);
            return;
        }
        run_depth(depth - 1, next, leaves, callbacks_on_pool_workers);
    });
}

bool exactly_once(const std::vector<std::atomic<unsigned>>& leaves)
{
    return std::all_of(leaves.begin(), leaves.end(), [](const auto& value) {
        return value.load(std::memory_order_relaxed) == 1;
    });
}

bool report(const char* label, bool passed, const std::string& details)
{
    std::cout << label << ": " << (passed ? "PASS" : "FAIL")
              << " [" << details << "]\n";
    return passed;
}
} // namespace

int main()
{
    ConfigGuard guard;
    configure_regression_path();

    constexpr std::size_t depth = 4;
    constexpr std::size_t fanout = 4;
    constexpr std::size_t leaf_count = fanout * fanout * fanout * fanout;
    constexpr std::size_t stress_rounds = 6;

    std::vector<std::atomic<unsigned>> leaves(leaf_count);
    std::atomic<std::size_t> callbacks_on_pool_workers{0};
    std::promise<void> completion;
    auto completed = completion.get_future();

    smart::global_thread_pool().submit([&] {
        try
        {
            const auto parent = bounded_thread_pool_parent(24000);
            smart::detail::ExecutionContextScope parent_scope(parent);
            run_depth(depth, 0, leaves, callbacks_on_pool_workers);
            completion.set_value();
        }
        catch (...)
        {
            completion.set_exception(std::current_exception());
        }
    });

    const bool completed_in_time =
        completed.wait_for(std::chrono::seconds(20)) == std::future_status::ready;
    if (completed_in_time)
        completed.get();

    const bool exact = completed_in_time && exactly_once(leaves);
    const bool worker_reentry = completed_in_time
        && callbacks_on_pool_workers.load(std::memory_order_relaxed) > 0;

    const bool completion_ok = report(
        "Automatic depth-four public parallel_for completes without deadlock",
        completed_in_time,
        "depth=4, timeout=20s");
    const bool exact_ok = report(
        "Automatic profiling and gap execution remain exact-once",
        exact,
        "leaves=" + std::to_string(leaf_count));
    const bool reentry_ok = report(
        "ThreadPool worker re-entry remains scheduler-safe",
        worker_reentry,
        "worker_callbacks="
            + std::to_string(callbacks_on_pool_workers.load(std::memory_order_relaxed)));

    bool stress_ok = completed_in_time;
    std::size_t stress_leaves = 0;
    if (stress_ok)
    {
        for (std::size_t round = 0; round < stress_rounds; ++round)
        {
            std::vector<std::atomic<unsigned>> round_leaves(leaf_count);
            std::atomic<std::size_t> round_worker_callbacks{0};
            const auto parent = bounded_thread_pool_parent(25000 + round);
            smart::detail::ExecutionContextScope parent_scope(parent);
            run_depth(depth, 0, round_leaves, round_worker_callbacks);
            stress_ok = stress_ok && exactly_once(round_leaves);
            stress_leaves += leaf_count;
        }
    }
    report("Repeated automatic depth-four execution remains stable",
           stress_ok,
           "rounds=" + std::to_string(stress_rounds)
               + ", leaves=" + std::to_string(stress_leaves));

    const bool passed = completion_ok && exact_ok && reentry_ok && stress_ok;
    std::cout << (passed
        ? "PASS: automatic depth-four public parallel_for regression is fixed.\n"
        : "FAIL: automatic depth-four public parallel_for regression remains.\n");
    return passed ? 0 : 1;
}
