#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include <smart/execution/backend.hpp>
#include <smart/execution/parallel.hpp>

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

void configure_nested(smart::ExecutionEngineType engine = smart::ExecutionEngineType::ThreadPool)
{
    auto& config = smart::global_config();
    config.enable_experience = false;
    config.enable_utility_model_runtime = false;
    config.execution_engine = engine;
    config.enable_parallel_for_profile_cache = true;
    config.enable_parallel_for_auto_profiling = true;
    config.enable_nested_execution_session = true;
    config.enable_nested_root_online_telemetry = true;
    config.enable_nested_online_telemetry = true;
    config.enable_nested_parallel_frontier = true;
    config.nested_root_concurrency_budget = 4;
    config.nested_min_parallel_work_ms = 0.001;
    config.nested_plan_hysteresis = 1.0;
    config.enable_nested_execution_trace = true;
    smart::global_function_profile_cache().clear();
    smart::clear_nested_execution_trace();
}

void burn(std::size_t rounds, std::size_t seed)
{
    volatile std::uint64_t value = static_cast<std::uint64_t>(seed + 1);
    for (std::size_t round = 0; round < rounds; ++round)
        value = value * 6364136223846793005ull + 1442695040888963407ull;
    (void)value;
}

void run_irregular(std::vector<std::atomic<unsigned>>& visits)
{
    constexpr std::size_t outer = 8;
    constexpr std::size_t child_capacity = 17;
    constexpr std::size_t grand_capacity = 9;
    smart::parallel_for(0, outer, [&](std::size_t i)
    {
        const std::size_t children = i % 3 == 0 ? 1 : (i % 3 == 1 ? 3 : 17);
        smart::parallel_for(0, children, [&](std::size_t j)
        {
            const std::size_t grandchildren = (i + j) % 4 == 0 ? 9 : ((i + j) % 2 == 0 ? 2 : 1);
            smart::parallel_for(0, grandchildren, [&](std::size_t k)
            {
                const std::size_t index = (i * child_capacity + j) * grand_capacity + k;
                visits[index].fetch_add(1, std::memory_order_relaxed);
                burn(i == 7 && j == 16 ? 2000 : 30 + (i * 7 + j * 3 + k), index);
            });
        });
    });
}

void test_irregular_tree_exactly_once()
{
    configure_nested();
    constexpr std::size_t outer = 8;
    constexpr std::size_t child_capacity = 17;
    constexpr std::size_t grand_capacity = 9;
    std::vector<std::atomic<unsigned>> visits(outer * child_capacity * grand_capacity);
    for (auto& visit : visits)
        visit.store(0, std::memory_order_relaxed);

    run_irregular(visits); // conservative learning
    for (auto& visit : visits)
        visit.store(0, std::memory_order_relaxed);
    smart::clear_nested_execution_trace();
    run_irregular(visits); // cached irregular policy

    for (std::size_t i = 0; i < outer; ++i)
    {
        const std::size_t children = i % 3 == 0 ? 1 : (i % 3 == 1 ? 3 : 17);
        for (std::size_t j = 0; j < child_capacity; ++j)
        {
            const std::size_t grandchildren = j < children
                ? ((i + j) % 4 == 0 ? 9 : ((i + j) % 2 == 0 ? 2 : 1))
                : 0;
            for (std::size_t k = 0; k < grand_capacity; ++k)
            {
                const std::size_t index = (i * child_capacity + j) * grand_capacity + k;
                const unsigned expected = j < children && k < grandchildren ? 1u : 0u;
                require(visits[index].load(std::memory_order_relaxed) == expected,
                        "irregular tree skipped or duplicated a leaf");
            }
        }
    }

    std::size_t max_leases = 0;
    for (const auto& record : smart::nested_execution_trace_snapshot())
        max_leases = std::max(max_leases, record.max_root_leased_workers);
    require(max_leases <= 4, "irregular tree exceeded the root lease budget");
}

void test_nested_exception_and_recovery()
{
    configure_nested();
    std::atomic<bool> throw_now{false};
    auto workload = [&]
    {
        smart::parallel_for(0, 8, [&](std::size_t i)
        {
            smart::parallel_for(0, 32, [&](std::size_t j)
            {
                smart::parallel_for(0, 16, [&](std::size_t k)
                {
                    if (throw_now.load(std::memory_order_relaxed) && i == 3 && j == 7 && k == 5)
                        throw std::runtime_error("expected nested exception");
                    burn(20, i * 1000 + j * 32 + k);
                });
            });
        });
    };

    workload();
    throw_now.store(true, std::memory_order_relaxed);
    bool threw = false;
    try
    {
        workload();
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    require(threw, "nested helper exception was swallowed");

    throw_now.store(false, std::memory_order_relaxed);
    std::atomic<std::size_t> visits{0};
    smart::parallel_for(0, 8, [&](std::size_t)
    {
        smart::parallel_for(0, 32, [&](std::size_t)
        {
            visits.fetch_add(1, std::memory_order_relaxed);
        });
    });
    require(visits.load(std::memory_order_relaxed) == 8 * 32,
            "nested scheduler did not recover after an exception");
}

void test_lease_exhaustion_falls_back_without_deadlock()
{
    auto session = std::make_shared<smart::NestedExecutionSession>(2, 500);
    std::atomic<std::size_t> visits{0};

    smart::BackendExecutionRequest outer;
    outer.total = 2;
    outer.concurrency_budget = 2;
    outer.chunk_size = 1;
    outer.nested_session = session;
    outer.function = [&](std::size_t)
    {
        smart::BackendExecutionRequest inner;
        inner.total = 8;
        inner.concurrency_budget = 2;
        inner.chunk_size = 1;
        inner.cooperative_helping = true;
        inner.nested_session = session;
        inner.function = [&](std::size_t)
        {
            visits.fetch_add(1, std::memory_order_relaxed);
        };
        smart::execution_backend(smart::ExecutionEngineType::ThreadPool)
            .execute(std::move(inner));
    };
    smart::execution_backend(smart::ExecutionEngineType::ThreadPool)
        .execute(std::move(outer));

    require(visits.load(std::memory_order_relaxed) == 16,
            "lease-exhausted nested work did not complete exactly once");
    require(session->maximum_leased_workers() <= 2,
            "lease exhaustion exceeded the configured budget");
    require(session->leased_workers() == 0, "lease exhaustion leaked a permit");
    require(session->lease_invariant_violations() == 0,
            "lease exhaustion violated accounting invariants");
}

void test_backend_switching_correctness()
{
    for (const auto engine : {smart::ExecutionEngineType::ThreadPool,
                              smart::ExecutionEngineType::StaticThread,
                              smart::ExecutionEngineType::OneTbb})
    {
        configure_nested(engine);
        std::vector<std::atomic<unsigned>> visits(4 * 12);
        for (auto& visit : visits)
            visit.store(0, std::memory_order_relaxed);
        smart::parallel_for(0, 4, [&](std::size_t i)
        {
            smart::parallel_for(0, 12, [&](std::size_t j)
            {
                visits[i * 12 + j].fetch_add(1, std::memory_order_relaxed);
            });
        });
        for (const auto& visit : visits)
            require(visit.load(std::memory_order_relaxed) == 1,
                    "backend switch skipped or duplicated nested work");
    }
}

void test_concurrent_root_progress()
{
    configure_nested();
    auto long_root = std::async(std::launch::async, []
    {
        smart::parallel_for(0, 256, [](std::size_t i) { burn(2000, i); });
    });
    auto short_root = std::async(std::launch::async, []
    {
        std::atomic<std::size_t> visits{0};
        smart::parallel_for(0, 64, [&](std::size_t)
        {
            visits.fetch_add(1, std::memory_order_relaxed);
        });
        require(visits.load(std::memory_order_relaxed) == 64,
                "short concurrent root lost progress");
    });

    require(short_root.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
            "short root starved behind another root");
    short_root.get();
    require(long_root.wait_for(std::chrono::seconds(10)) == std::future_status::ready,
            "long concurrent root did not complete");
    long_root.get();
}

void test_parallel_plan_periodic_revalidation()
{
    configure_nested();
    auto& config = smart::global_config();
    config.parallel_for_stable_plan_revalidate_interval = 1;
    config.parallel_for_profile_min_signal_ms = 0.000001;
    config.parallel_for_sequential_fast_path_min_observations = 1;
    config.enable_nested_execution_trace = true;
    smart::global_function_profile_cache().clear();

    auto workload = [](std::size_t i)
    {
        smart::parallel_for(0, 8, [i](std::size_t j) { burn(100, i * 8 + j); });
    };
    smart::parallel_for(0, 8, workload); // full online learning
    smart::parallel_for(0, 8, workload); // decide and store stable plan
    smart::parallel_for(0, 8, workload); // consume one stable-plan use
    smart::clear_nested_execution_trace();
    smart::parallel_for(0, 8, workload); // periodic post-execution revalidation

    bool saw_revalidation = false;
    for (const auto& record : smart::nested_execution_trace_snapshot())
    {
        if (record.depth == 1 && record.decision_reason == "root_online_revalidation")
            saw_revalidation = true;
    }
    require(saw_revalidation, "stable nested plan was never periodically revalidated");
}
} // namespace

int main()
{
    try
    {
        ConfigGuard guard;
        test_irregular_tree_exactly_once();
        test_nested_exception_and_recovery();
        test_lease_exhaustion_falls_back_without_deadlock();
        test_backend_switching_correctness();
        test_concurrent_root_progress();
        test_parallel_plan_periodic_revalidation();
        std::cout << "nested irregular validation: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "nested irregular validation: FAIL: " << error.what() << '\n';
        return 1;
    }
}
