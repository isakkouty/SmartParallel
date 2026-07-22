#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <smart/decision/backend_calibration.hpp>
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

void burn(std::size_t seed)
{
    volatile std::uint64_t value = static_cast<std::uint64_t>(seed + 1);
    for (std::size_t round = 0; round < 48; ++round)
        value = (value ^ (value >> 11)) * 0x9E3779B97F4A7C15ull + round;
    (void)value;
}

void test_analytical_cold_root_and_direct_descendants()
{
    ConfigGuard guard;
    auto& config = smart::global_config();
    config.enable_experience = false;
    config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    config.nested_root_concurrency_budget = 4;
    config.enable_nested_execution_trace = true;
    config.enable_root_analytical_cold_start = true;
    config.root_analytical_cold_min_iterations_per_worker = 4;
    config.enable_frontier_descendant_direct_mode = true;
    config.enable_parallel_for_backend_calibration = false;
    smart::global_function_profile_cache().clear();
    smart::clear_nested_execution_trace();

    constexpr std::size_t outer = 64;
    constexpr std::size_t inner = 8;
    std::vector<std::atomic<unsigned>> visits(outer * inner);
    for (auto& value : visits)
        value.store(0, std::memory_order_relaxed);

    smart::parallel_for(0, outer, [&](std::size_t i)
    {
        smart::parallel_for(0, inner, [&](std::size_t j)
        {
            visits[i * inner + j].fetch_add(1, std::memory_order_relaxed);
            burn(i * inner + j);
        });
    });

    for (const auto& value : visits)
        require(value.load(std::memory_order_relaxed) == 1,
                "analytical cold root skipped or duplicated work");

    bool saw_analytical_cold = false;
    for (const auto& record : smart::nested_execution_trace_snapshot())
    {
        if (record.depth == 1
            && record.decision_reason == "root_analytical_cold_learning"
            && record.parallel)
            saw_analytical_cold = true;
    }
    require(saw_analytical_cold,
            "large cold root did not use analytical exactly-once learning");

    // Disable trace so the second execution uses the truly sealed path.
    config.enable_nested_execution_trace = false;
    for (auto& value : visits)
        value.store(0, std::memory_order_relaxed);
    const std::size_t cache_before = smart::global_function_profile_cache().size();
    smart::parallel_for(0, outer, [&](std::size_t i)
    {
        smart::parallel_for(0, inner, [&](std::size_t j)
        {
            visits[i * inner + j].fetch_add(1, std::memory_order_relaxed);
        });
    });
    const std::size_t cache_after = smart::global_function_profile_cache().size();
    for (const auto& value : visits)
        require(value.load(std::memory_order_relaxed) == 1,
                "sealed descendant direct mode skipped or duplicated work");
    require(cache_after <= cache_before + 1,
            "sealed descendants unexpectedly populated per-child profile state");
}


void test_pilot_cold_root_is_exactly_once()
{
    ConfigGuard guard;
    auto& config = smart::global_config();
    config.enable_experience = false;
    config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    config.nested_root_concurrency_budget = 4;
    config.enable_nested_execution_trace = true;
    config.enable_root_analytical_cold_start = true;
    config.root_analytical_cold_min_iterations_per_worker = 4;
    config.enable_root_pilot_cold_start = true;
    config.root_pilot_cold_min_estimated_work_ms = 0.0;
    config.enable_parallel_for_backend_calibration = false;
    smart::global_function_profile_cache().clear();
    smart::clear_nested_execution_trace();

    std::array<std::atomic<unsigned>, 4> visits{};
    smart::parallel_for(0, visits.size(), [&](std::size_t i)
    {
        visits[i].fetch_add(1, std::memory_order_relaxed);
        for (std::size_t round = 0; round < 2048; ++round)
            burn(i + round);
    });

    for (const auto& value : visits)
        require(value.load(std::memory_order_relaxed) == 1,
                "pilot cold root skipped or duplicated work");

    bool saw_pilot = false;
    for (const auto& record : smart::nested_execution_trace_snapshot())
    {
        if (record.depth == 1
            && record.decision_reason == "root_pilot_cold_learning"
            && record.parallel)
            saw_pilot = true;
    }
    require(saw_pilot,
            "coarse cold root did not promote the remaining exactly-once work");
}

void test_tiny_nested_root_bypass()
{
    ConfigGuard guard;
    auto& config = smart::global_config();
    config.enable_experience = false;
    config.execution_engine = smart::ExecutionEngineType::Auto;
    config.nested_root_concurrency_budget = 4;
    config.enable_nested_execution_trace = true;
    config.enable_parallel_for_tiny_work_bypass = true;
    config.parallel_for_tiny_work_bypass_max_ms = 1000.0;
    config.parallel_for_tiny_work_bypass_min_observations = 1;
    config.parallel_for_sequential_fast_path_min_observations = 1;
    config.parallel_for_minimum_predicted_speedup = 1000.0;
    config.parallel_for_stable_plan_revalidate_interval = 0;
    config.parallel_for_profile_revalidate_after_ms = 0;
    config.enable_parallel_for_backend_calibration = false;
    smart::global_function_profile_cache().clear();

    const auto run = [&]
    {
        smart::parallel_for(0, std::size_t{1}, [&](std::size_t)
        {
            smart::parallel_for(0, std::size_t{8}, [&](std::size_t j)
            {
                burn(j);
            });
        });
    };

    for (std::size_t observation = 0; observation < 4; ++observation)
        run();

    smart::clear_nested_execution_trace();
    run();
    bool saw_bypass = false;
    for (const auto& record : smart::nested_execution_trace_snapshot())
    {
        if (record.depth == 1
            && record.decision_reason == "tiny_work_absolute_bypass"
            && !record.parallel)
            saw_bypass = true;
    }
    require(saw_bypass,
            "stable tiny nested root did not use the absolute-cost bypass");
}


void test_tiny_root_requires_sequential_profitability()
{
    ConfigGuard guard;
    auto& config = smart::global_config();
    config.enable_experience = false;
    config.execution_engine = smart::ExecutionEngineType::Auto;
    config.nested_root_concurrency_budget = 4;
    config.enable_nested_execution_trace = true;
    config.enable_parallel_for_tiny_work_bypass = true;
    config.parallel_for_tiny_work_bypass_max_ms = 1000.0;
    config.parallel_for_tiny_work_bypass_min_observations = 1;
    config.parallel_for_sequential_fast_path_min_observations = 1;
    config.parallel_for_minimum_predicted_speedup = 0.0;
    config.parallel_for_stable_plan_revalidate_interval = 0;
    config.parallel_for_profile_revalidate_after_ms = 0;
    config.enable_parallel_for_backend_calibration = false;
    smart::global_function_profile_cache().clear();

    const auto run = []
    {
        smart::parallel_for(0, std::size_t{64}, [](std::size_t i)
        {
            burn(i);
        });
    };

    for (std::size_t observation = 0; observation < 4; ++observation)
        run();

    smart::clear_nested_execution_trace();
    run();
    for (const auto& record : smart::nested_execution_trace_snapshot())
    {
        require(record.decision_reason != "tiny_work_absolute_bypass",
                "tiny root bypass ignored profitability evidence");
    }
}

void test_profile_revalidation_freeze()
{
    smart::global_function_profile_cache().clear();
    smart::FunctionProfileKey key{};
    key.function_hash = 998877;
    key.iteration_bucket = 8;
    smart::FunctionProfile profile;
    profile.available = true;
    profile.samples = 8;
    profile.avg_ms_per_iteration = 0.01;
    profile.median_ms_per_iteration = 0.01;
    profile.trimmed_mean_ms_per_iteration = 0.01;
    profile.p95_ms_per_iteration = 0.01;
    profile.max_ms_per_iteration = 0.01;
    profile.estimated_total_work_ms = 0.08;
    profile.parallel_worthiness = 0.5;
    smart::global_function_profile_cache().store(key, profile);

    smart::global_function_profile_cache().freeze_revalidation();
    require(smart::global_function_profile_cache().revalidation_frozen(),
            "profile cache did not enter frozen revalidation mode");
    auto frozen = smart::global_function_profile_cache().try_acquire_revalidation(key);
    require(!frozen.owns_revalidation(),
            "frozen profile cache allowed steady-state revalidation");

    smart::global_function_profile_cache().unfreeze_revalidation();
    require(!smart::global_function_profile_cache().revalidation_frozen(),
            "profile cache did not leave frozen revalidation mode");
    auto active = smart::global_function_profile_cache().try_acquire_revalidation(key);
    require(active.owns_revalidation(),
            "profile cache did not restore production revalidation");
}

void test_session_plan_memo_is_bounded()
{
    ConfigGuard guard;
    auto& config = smart::global_config();
    config.nested_plan_snapshot_max_entries = 4;
    auto session = std::make_shared<smart::NestedExecutionSession>(4, 1);
    for (std::size_t i = 0; i < 32; ++i)
    {
        smart::NestedPlanSnapshotKey key;
        key.function_hash = i + 1;
        key.exact_iterations = 16 + i;
        smart::ExecutionPlan plan;
        plan.parallel = (i & 1u) != 0;
        session->store_plan_snapshot(key, plan);
    }
    require(session->plan_snapshot_count() <= 4,
            "session-local resolved-plan memo exceeded its configured bound");
}

void test_backend_calibration_cache()
{
    ConfigGuard guard;
    auto& config = smart::global_config();
    config.enable_parallel_for_backend_calibration = true;
    config.parallel_for_backend_calibration_min_samples = 1;
    config.parallel_for_backend_calibration_hysteresis_percent = 8.0;
    config.parallel_for_backend_calibration_max_entries = 8;
    smart::global_backend_calibration_cache().clear();

    smart::FunctionProfileKey key{};
    key.function_hash = 12345;
    key.iteration_bucket = 1024;
    key.concurrency_budget = 4;
    const std::uint64_t generation = 7;

    auto selected = smart::global_backend_calibration_cache().select(
        key, generation, smart::ExecutionEngineType::ThreadPool);
    require(selected == smart::ExecutionEngineType::ThreadPool,
            "calibration did not begin with the requested backend");
    smart::global_backend_calibration_cache().record(
        key, generation, smart::ExecutionEngineType::ThreadPool, 10.0);

#if SMARTPARALLEL_HAS_TBB
    selected = smart::global_backend_calibration_cache().select(
        key, generation, smart::ExecutionEngineType::ThreadPool);
    require(selected == smart::ExecutionEngineType::OneTbb,
            "calibration did not probe the available alternative backend");
    smart::global_backend_calibration_cache().record(
        key, generation, smart::ExecutionEngineType::OneTbb, 7.0);
    selected = smart::global_backend_calibration_cache().select(
        key, generation, smart::ExecutionEngineType::ThreadPool);
    require(selected == smart::ExecutionEngineType::OneTbb,
            "calibration did not retain the measured backend winner");
#else
    selected = smart::global_backend_calibration_cache().select(
        key, generation, smart::ExecutionEngineType::ThreadPool);
    require(selected == smart::ExecutionEngineType::ThreadPool,
            "calibration selected an unavailable oneTBB backend");
#endif
    smart::global_backend_calibration_cache().freeze();
    require(smart::global_backend_calibration_cache().frozen(),
            "backend calibration cache did not enter frozen mode");
    selected = smart::global_backend_calibration_cache().select(
        key, generation, smart::ExecutionEngineType::ThreadPool);
#if SMARTPARALLEL_HAS_TBB
    require(selected == smart::ExecutionEngineType::OneTbb,
            "frozen calibration did not retain its resolved winner");
#else
    require(selected == smart::ExecutionEngineType::ThreadPool,
            "frozen calibration changed the available backend");
#endif
    smart::global_backend_calibration_cache().unfreeze();
    require(!smart::global_backend_calibration_cache().frozen(),
            "backend calibration cache did not leave frozen mode");
    require(smart::global_backend_calibration_cache().size() <= 8,
            "backend calibration cache exceeded its configured bound");
}

void test_causal_helper_metrics()
{
    ConfigGuard guard;
    smart::global_config().thread_pool_cancel_idle_helpers = true;
    smart::ThreadPool pool(2);
    smart::SchedulerVisibleWork work(0, 8, 1, smart::ExecutionContext{});
    const auto result = pool.execute_visible_work_helping(
        work, 2, [](const smart::WorkChunk&) {});
    require(result.in_flight_work_drain_ms >= 0.0,
            "negative in-flight work-drain metric");
    require(result.actual_blocking_wait_ms >= 0.0,
            "negative blocking-wait metric");
    require(result.completion_epilogue_ms >= 0.0,
            "negative completion-epilogue metric");
    if (result.wait_count == 0)
        require(result.completion_signal_to_waiter_wake_ms == 0.0,
                "notification wake was reported without an actual wait");
}
} // namespace

int main()
{
    try
    {
        test_analytical_cold_root_and_direct_descendants();
        test_pilot_cold_root_is_exactly_once();
        test_tiny_nested_root_bypass();
        test_tiny_root_requires_sequential_profitability();
        test_profile_revalidation_freeze();
        test_session_plan_memo_is_bounded();
        test_backend_calibration_cache();
        test_causal_helper_metrics();
        std::cout << "Real-world optimization hardening tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Real-world optimization hardening failure: " << error.what() << '\n';
        return 1;
    }
}
