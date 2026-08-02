#include <smart/execution/parallel.hpp>
#include <smart/runtime/runtime.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

smart::RuntimeOptions options(const std::shared_ptr<smart::ResourceGovernor>& governor,
                              std::size_t workers)
{
    smart::RuntimeOptions value;
    value.governor = governor;
    value.maximum_workers = workers;
    value.lease_wait_policy = smart::LeaseWaitPolicy::Wait;
    value.scheduler_config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    value.scheduler_config.enable_parallel_for_auto_profiling = false;
    value.scheduler_config.enable_parallel_for_profile_cache = false;
    value.scheduler_config.enable_parallel_for_backend_calibration = false;
    value.scheduler_config.enable_experience = false;
    value.scheduler_config.enable_nested_execution_session = true;
    value.scheduler_config.nested_min_iterations_per_worker = 1;
    value.scheduler_config.nested_min_parallel_work_ms = 0.0;
    value.scheduler_config.parallel_for_estimated_overhead_ms = 0.0;
    value.scheduler_config.parallel_for_minimum_predicted_speedup = 0.0;
    value.scheduler_config.small_workload_iteration_threshold = 0;
    value.scheduler_config.cheap_workload_sequential_threshold = 0;
    return value;
}

void update_maximum(std::atomic<std::size_t>& maximum, std::size_t value)
{
    auto observed = maximum.load(std::memory_order_relaxed);
    while (value > observed
           && !maximum.compare_exchange_weak(observed, value,
                                             std::memory_order_relaxed)) {}
}


void operation_specific_admission()
{
    auto governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{4, 4});
    auto configured = options(governor, 4);
    configured.scheduler_config.small_workload_iteration_threshold = 64;
    smart::Runtime runtime(configured);

    std::atomic<std::size_t> visits{0};
    smart::parallel_for(runtime.context(), std::size_t{0}, std::size_t{8},
        [&](std::size_t) { visits.fetch_add(1, std::memory_order_relaxed); });
    require(visits.load() == 8, "small governed operation produced incorrect output");
    const auto report = runtime.last_resource_decision_report();
    require(report.minimum_workers == 1 && report.preferred_workers == 1
                && report.maximum_workers == 4,
            "small operation did not publish distinct concurrency bounds");
    require(report.requested_workers == 1 && report.granted_workers == 1,
            "small operation blindly reserved the Runtime ceiling");

    auto held = governor->acquire([]
    {
        smart::LeaseRequest request;
        request.requested_workers = 3;
        request.minimum_workers = 3;
        request.preferred_workers = 3;
        request.maximum_workers = 3;
        request.exact_grant_required = true;
        return request;
    }());
    require(static_cast<bool>(held), "partial-grant setup failed");

    configured.scheduler_config.small_workload_iteration_threshold = 1;
    smart::Runtime partial(configured);
    visits.store(0, std::memory_order_relaxed);
    smart::parallel_for(partial.context(), std::size_t{0}, std::size_t{256},
        [&](std::size_t) { visits.fetch_add(1, std::memory_order_relaxed); });
    require(visits.load() == 256, "partial Adaptive grant produced incorrect output");
    const auto partial_report = partial.last_resource_decision_report();
    require(partial_report.minimum_workers == 1
                && partial_report.preferred_workers == 4
                && partial_report.maximum_workers == 4,
            "Adaptive request lost minimum/preferred/maximum semantics");
    require(partial_report.granted_workers == 1,
            "Adaptive request did not accept the available partial grant");
    require(partial_report.scheduler_concurrency_cap <= 1
                && partial_report.observed_participating_threads <= 1,
            "Adaptive plan was not revised to the actual grant");
}

void many_runtime_scaling(std::size_t runtime_count)
{
    constexpr std::size_t budget = 4;
    auto governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{budget, 8});
    std::vector<std::unique_ptr<smart::Runtime>> runtimes;
    for (std::size_t index = 0; index < runtime_count; ++index)
        runtimes.push_back(std::make_unique<smart::Runtime>(options(governor, budget)));

    std::atomic<std::size_t> active{0};
    std::atomic<std::size_t> maximum{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    for (auto& runtime : runtimes)
    {
        threads.emplace_back([&runtime, &active, &maximum, &start]
        {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            smart::parallel_for(runtime->context(), std::size_t{0}, std::size_t{96},
                [&](std::size_t index)
                {
                    const auto current = active.fetch_add(1, std::memory_order_acq_rel) + 1;
                    update_maximum(maximum, current);
                    volatile std::size_t value = index + 1;
                    for (std::size_t spin = 0; spin < 200; ++spin)
                        value = value * 1664525u + 1013904223u;
                    (void)value;
                    active.fetch_sub(1, std::memory_order_acq_rel);
                });
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();
    require(maximum.load(std::memory_order_relaxed) <= budget,
            "multi-Runtime scaling exceeded the shared budget");
    const auto snapshot = governor->snapshot();
    require(snapshot.active_permits == 0, "multi-Runtime scaling leaked permits");
    require(snapshot.total_grants == runtime_count,
            "multi-Runtime scaling did not admit exactly one root lease per Runtime");
}

void deep_nested_context_reuse()
{
    auto governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{4, 4});
    smart::Runtime runtime(options(governor, 4));
    std::atomic<std::size_t> leaves{0};
    std::atomic<std::size_t> active{0};
    std::atomic<std::size_t> maximum{0};

    smart::parallel_for(runtime.context(), std::size_t{0}, std::size_t{2},
        [&](std::size_t)
        {
            smart::parallel_for(std::size_t{0}, std::size_t{2}, [&](std::size_t)
            {
                smart::parallel_for(std::size_t{0}, std::size_t{2}, [&](std::size_t)
                {
                    smart::parallel_for(std::size_t{0}, std::size_t{4}, [&](std::size_t)
                    {
                        const auto current = active.fetch_add(1) + 1;
                        update_maximum(maximum, current);
                        leaves.fetch_add(1, std::memory_order_relaxed);
                        active.fetch_sub(1);
                    });
                });
            });
        });
    require(leaves.load() == 32, "depth-four nested execution lost work");
    require(maximum.load() <= 4, "nested execution exceeded its parent grant");
    require(governor->snapshot().total_grants == 1,
            "deep nested execution independently reacquired the governor");
}

void stable_resource_fingerprint()
{
    auto governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{2, 4});
    smart::Runtime runtime(options(governor, 2));
    smart::parallel_for(runtime.context(), std::size_t{0}, std::size_t{128},
        [](std::size_t) {});
    const auto first = runtime.last_resource_decision_report().stable_fingerprint;
    smart::parallel_for(runtime.context(), std::size_t{0}, std::size_t{128},
        [](std::size_t) {});
    const auto second = runtime.last_resource_decision_report().stable_fingerprint;
    require(!first.empty() && first == second,
            "volatile admission data leaked into the stable resource fingerprint");
}

void multi_runtime_shared_budget()
{
    auto governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{2, 4});
    smart::Runtime a(options(governor, 2));
    smart::Runtime b(options(governor, 2));

    std::atomic<std::size_t> active{0};
    std::atomic<std::size_t> maximum{0};
    std::atomic<bool> start{false};
    auto run = [&](smart::Runtime& runtime)
    {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        smart::parallel_for(runtime.context(), std::size_t{0}, std::size_t{64},
            [&](std::size_t)
            {
                const auto current = active.fetch_add(1, std::memory_order_acq_rel) + 1;
                update_maximum(maximum, current);
                std::this_thread::sleep_for(std::chrono::microseconds(20));
                active.fetch_sub(1, std::memory_order_acq_rel);
            });
    };

    std::thread first(run, std::ref(a));
    std::thread second(run, std::ref(b));
    start.store(true, std::memory_order_release);
    first.join();
    second.join();

    require(maximum.load() <= 2, "shared governor allowed participation above budget");
    const auto snapshot = governor->snapshot();
    require(snapshot.active_permits == 0, "shared Runtime execution leaked permits");
    require(snapshot.total_grants == 2, "each root Runtime call should acquire one lease");
    require(a.fingerprint().hash != b.fingerprint().hash || a.options().maximum_workers == b.options().maximum_workers,
            "Runtime fingerprints are unexpectedly invalid");
}

void nested_reuses_parent()
{
    auto governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{3, 4});
    smart::Runtime runtime(options(governor, 3));
    std::atomic<std::size_t> visits{0};

    smart::parallel_for(runtime.context(), std::size_t{0}, std::size_t{4},
        [&](std::size_t)
        {
            smart::parallel_for(std::size_t{0}, std::size_t{8},
                [&](std::size_t) { visits.fetch_add(1, std::memory_order_relaxed); });
        });

    require(visits.load() == 32, "nested governed execution produced incorrect output");
    const auto snapshot = governor->snapshot();
    require(snapshot.total_grants == 1,
            "nested execution independently reacquired the process governor");
    require(snapshot.active_permits == 0, "nested execution leaked the parent lease");
}

void deterministic_failure_is_pre_execution()
{
    auto governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{2, 2});
    auto held = governor->acquire([]
    {
        smart::LeaseRequest request;
        request.requested_workers = 1;
        request.minimum_workers = 1;
        request.exact_grant_required = true;
        return request;
    }());
    require(static_cast<bool>(held), "deterministic setup could not reserve one permit");

    auto deterministic = options(governor, 2);
    deterministic.execution_mode = smart::ExecutionMode::Deterministic;
    deterministic.profile_access = smart::ProfileAccess::Disabled;
    deterministic.lease_wait_policy = smart::LeaseWaitPolicy::FailImmediately;
    deterministic.scheduler_config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    smart::Runtime runtime(deterministic);

    std::vector<int> output(128, 7);
    const auto before = output;
    bool rejected = false;
    try
    {
        smart::parallel_for(runtime.context(), std::size_t{0}, output.size(),
            [&](std::size_t index) { output[index] = 11; });
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    require(rejected, "deterministic exact grant was silently reduced");
    require(output == before, "deterministic admission failure modified output");
    const auto report = runtime.last_resource_decision_report();
    require(report.admission_status == smart::LeaseAcquireStatus::WouldBlock,
            "deterministic rejection report has the wrong status");
    require(report.exact_grant_required, "deterministic report lost exact-grant requirement");
}


#if SMARTPARALLEL_HAS_TBB
void one_tbb_upper_bound_contract()
{
    auto governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{3, 4});
    auto configured = options(governor, 3);
    configured.scheduler_config.execution_engine = smart::ExecutionEngineType::OneTbb;
    configured.execution_mode = smart::ExecutionMode::Deterministic;
    configured.profile_access = smart::ProfileAccess::Disabled;
    configured.lease_wait_policy = smart::LeaseWaitPolicy::FailImmediately;
    smart::Runtime runtime(configured);

    std::atomic<std::size_t> visits{0};
    smart::parallel_for(runtime.context(), std::size_t{0}, std::size_t{1024},
        [&](std::size_t)
        {
            visits.fetch_add(1, std::memory_order_relaxed);
        });
    require(visits.load() == 1024, "oneTBB governed execution produced incorrect output");
    const auto report = runtime.last_resource_decision_report();
    require(report.scheduler == "one_tbb", "oneTBB scheduler identity was not reported");
    require(report.exact_grant_required && report.granted_workers == 3,
            "deterministic oneTBB replay did not preserve the exact lease grant");
    require(report.scheduler_concurrency_cap <= 3, "oneTBB arena cap exceeded the lease grant");
    require(report.observed_participating_threads >= 1
                && report.observed_participating_threads
                    <= report.scheduler_concurrency_cap,
            "oneTBB observed participation exceeded its arena cap");
    require(report.provider_control_scope == smart::ControlScope::PerTask
                && report.provider_control_strength == smart::ControlStrength::UpperBound,
            "oneTBB control capabilities were reported dishonestly");

    const auto grants_before_nested = governor->snapshot().total_grants;
    std::atomic<std::size_t> nested_visits{0};
    smart::parallel_for(runtime.context(), std::size_t{0}, std::size_t{32},
        [&](std::size_t index)
        {
            if (index == 0)
            {
                smart::parallel_for(std::size_t{0}, std::size_t{16},
                    [&](std::size_t)
                    {
                        nested_visits.fetch_add(1, std::memory_order_relaxed);
                    });
            }
        });
    require(nested_visits.load() == 16, "nested oneTBB execution produced incorrect output");
    require(governor->snapshot().total_grants == grants_before_nested + 1,
            "nested oneTBB execution independently reacquired the governor");

    bool threw = false;
    try
    {
        smart::parallel_for(runtime.context(), std::size_t{0}, std::size_t{64},
            [&](std::size_t index)
            {
                if (index == 7) throw std::runtime_error("injected oneTBB failure");
            });
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    require(threw, "oneTBB callback exception was not propagated");
    require(governor->snapshot().active_permits == 0,
            "oneTBB exception unwinding leaked governor permits");
}
#endif

void separate_governors_are_isolated()
{
    auto first_governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{1, 2});
    auto second_governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{1, 2});
    smart::Runtime first(options(first_governor, 1));
    smart::Runtime second(options(second_governor, 1));

    std::atomic<bool> first_entered{false};
    std::atomic<bool> second_entered{false};
    std::thread a([&]
    {
        smart::parallel_for(first.context(), std::size_t{0}, std::size_t{1}, [&](std::size_t)
        {
            first_entered.store(true, std::memory_order_release);
            while (!second_entered.load(std::memory_order_acquire)) std::this_thread::yield();
        });
    });
    std::thread b([&]
    {
        smart::parallel_for(second.context(), std::size_t{0}, std::size_t{1}, [&](std::size_t)
        {
            second_entered.store(true, std::memory_order_release);
            while (!first_entered.load(std::memory_order_acquire)) std::this_thread::yield();
        });
    });
    a.join();
    b.join();
    require(first_governor->snapshot().total_grants == 1
            && second_governor->snapshot().total_grants == 1,
            "separate governors leaked accounting across Runtime instances");
}
}

int main()
{
    try
    {
        operation_specific_admission();
        multi_runtime_shared_budget();
        many_runtime_scaling(4);
        many_runtime_scaling(8);
        nested_reuses_parent();
        deep_nested_context_reuse();
        deterministic_failure_is_pre_execution();
        separate_governors_are_isolated();
        stable_resource_fingerprint();
#if SMARTPARALLEL_HAS_TBB
        one_tbb_upper_bound_contract();
#endif
        std::cout << "SmartParallel v1.8 Runtime governance validation passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SmartParallel v1.8 Runtime governance validation failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
