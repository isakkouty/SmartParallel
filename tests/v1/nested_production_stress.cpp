#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

#include <smart/execution/executor.hpp>
#include <smart/execution/parallel.hpp>
#include <smart/execution/thread_pool.hpp>
#include <smart/profiling/function_profile_cache.hpp>
#include <smart/workload/workload_builder.hpp>

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

smart::FunctionProfile profile_with_speedup(double speedup)
{
    smart::FunctionProfile profile;
    profile.available = true;
    profile.measurement_reliable = true;
    profile.stable = true;
    profile.samples = 8;
    profile.avg_ms_per_iteration = 0.01;
    profile.median_ms_per_iteration = 0.01;
    profile.trimmed_mean_ms_per_iteration = 0.01;
    profile.estimated_total_work_ms = 1.0;
    profile.parallel_worthiness = speedup;
    profile.metadata.confidence = smart::ObservationConfidence::High;
    return profile;
}

smart::FunctionProfileKey key_for(std::size_t identity)
{
    smart::FunctionProfileKey key;
    key.function_hash = identity;
    key.element_size = sizeof(std::size_t);
    key.iteration_bucket = 64;
    key.depth = 1;
    key.concurrency_budget = 4;
    key.engine = smart::ExecutionEngineType::ThreadPool;
    key.policy_signature = 17;
    return key;
}

void test_bounded_cache_and_active_entry_protection()
{
    auto& config = smart::global_config();
    config.parallel_for_profile_cache_max_entries = 2;
    smart::FunctionProfileCache cache;
    cache.store(key_for(1), profile_with_speedup(2.0));
    cache.store(key_for(2), profile_with_speedup(2.0));

    auto guard = cache.try_acquire_revalidation(key_for(1));
    require(guard.owns_revalidation(), "failed to acquire cache revalidation guard");
    cache.store(key_for(3), profile_with_speedup(2.0));

    require(cache.size() == 2, "profile cache exceeded its configured capacity");
    require(cache.find(key_for(1)).has_value(),
            "cache evicted an entry while it was being revalidated");
    require(cache.find(key_for(3)).has_value(), "cache did not retain the new entry");
}

void test_revalidation_is_single_flight()
{
    smart::FunctionProfileCache cache;
    const auto key = key_for(10);
    cache.store(key, profile_with_speedup(2.0));
    auto first = cache.try_acquire_revalidation(key);
    auto second = cache.try_acquire_revalidation(key);
    require(first.owns_revalidation(), "first revalidation owner was rejected");
    require(!second.owns_revalidation(), "concurrent cache revalidation was not single-flight");
    first = {};
    auto third = cache.try_acquire_revalidation(key);
    require(third.owns_revalidation(), "revalidation ownership was not released");
}

void test_nested_evidence_decays()
{
    auto& config = smart::global_config();
    config.parallel_for_profile_nested_evidence_blend = 0.5;
    config.parallel_for_profile_nested_evidence_threshold = 0.5;
    smart::FunctionProfileCache cache;
    const auto key = key_for(20);
    const auto profile = profile_with_speedup(2.0);

    cache.store(key, profile, 0.25, 1.10, 1, 4, 1.0);
    require(cache.find(key)->observed_nested_calls,
            "nested callsite was not classified as nested");
    cache.store(key, profile, 0.25, 1.10, 1, 0, 0.0);
    cache.store(key, profile, 0.25, 1.10, 1, 0, 0.0);
    const auto decayed = cache.find(key);
    require(decayed && !decayed->observed_nested_calls,
            "historical nesting evidence remained permanently sticky");
}

void test_policy_and_explicit_callsite_identity()
{
    smart::Config first;
    smart::Config second = first;
    second.nested_target_chunk_ms *= 2.0;
    require(smart::detail::parallel_policy_signature(first)
                != smart::detail::parallel_policy_signature(second),
            "material scheduler policy change did not invalidate cache identity");

    struct Reusable
    {
        void operator()(std::size_t) const {}
    };
    const auto callsite_a = smart::with_parallel_callsite(1001, Reusable{});
    const auto callsite_b = smart::with_parallel_callsite(1002, Reusable{});
    require(smart::detail::callable_identity_hash(callsite_a)
                != smart::detail::callable_identity_hash(callsite_b),
            "explicit callsite keys still aliased reusable functors");
}


void test_stale_plan_generation_and_clear_epoch()
{
    smart::FunctionProfileCache cache;
    const auto key = key_for(30);
    auto profile = profile_with_speedup(2.0);
    const std::uint64_t epoch = cache.cache_epoch();
    const std::uint64_t first_generation = cache.store(key, profile, 0.25, 1.10, 1, 0, 0.0, 1, epoch);
    require(first_generation != 0, "initial profile generation was not published");

    smart::ExecutionPlan old_plan;
    old_plan.parallel = true;
    old_plan.engine = smart::ExecutionEngineType::ThreadPool;
    old_plan.strategy = smart::ExecutionStrategy::DynamicChunks;
    old_plan.job_count = 4;

    profile.avg_ms_per_iteration *= 2.0;
    const std::uint64_t second_generation = cache.store(key, profile, 0.25, 1.10, 1, 0, 0.0, 2, epoch);
    require(second_generation != 0 && second_generation != first_generation,
            "profile update did not advance its generation");
    require(!cache.store_stable_plan(key, old_plan, first_generation),
            "stale plan was installed over a newer profile generation");
    require(cache.store_stable_plan(key, old_plan, second_generation),
            "current profile generation rejected its stable plan");

    cache.clear();
    require(cache.store(key, profile, 0.25, 1.10, 1, 0, 0.0, 3, epoch) == 0,
            "in-flight observation repopulated the cache after clear");
    require(cache.size() == 0, "cache clear was not an invalidation barrier");
    require(!cache.store_stable_plan(key, old_plan, 0),
            "generation-zero stable plan bypassed cache invalidation");

    const auto build_key = key_for(31);
    auto old_build = cache.try_acquire_profile_build(build_key);
    require(old_build.owns_build(), "failed to acquire pre-clear build ownership");
    cache.clear();
    auto blocked_build = cache.try_acquire_profile_build(build_key);
    require(!blocked_build.owns_build(),
            "cache clear duplicated an in-flight profile build owner");
    old_build = {};
    auto new_build = cache.try_acquire_profile_build(build_key);
    require(new_build.owns_build(),
            "profile build ownership did not recover after old guard release");

    const auto revalidation_key = key_for(32);
    const std::uint64_t current_epoch = cache.cache_epoch();
    cache.store(revalidation_key, profile, 0.25, 1.10, 1, 0, 0.0, 4, current_epoch);
    auto old_revalidation = cache.try_acquire_revalidation(revalidation_key);
    require(old_revalidation.owns_revalidation(),
            "failed to acquire pre-clear revalidation ownership");
    cache.clear();
    const std::uint64_t post_clear_epoch = cache.cache_epoch();
    cache.store(revalidation_key, profile, 0.25, 1.10, 1, 0, 0.0, 5, post_clear_epoch);
    auto blocked_revalidation = cache.try_acquire_revalidation(revalidation_key);
    require(!blocked_revalidation.owns_revalidation(),
            "cache clear duplicated an in-flight revalidation owner");
    old_revalidation = {};
    auto new_revalidation = cache.try_acquire_revalidation(revalidation_key);
    require(new_revalidation.owns_revalidation(),
            "revalidation ownership did not recover after old guard release");
}

void test_time_based_plan_revalidation()
{
    auto& config = smart::global_config();
    config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    config.enable_experience = false;
    config.enable_nested_execution_session = true;
    config.enable_nested_root_online_telemetry = true;
    config.enable_parallel_for_profile_cache = true;
    config.parallel_for_profile_revalidate_after_ms = 1;
    config.parallel_for_stable_plan_revalidate_interval = 1000000;
    config.nested_root_concurrency_budget = 2;
    config.enable_nested_execution_trace = true;
    smart::global_function_profile_cache().clear();
    smart::clear_nested_execution_trace();

    auto workload = smart::with_parallel_callsite(0xA501, [](std::size_t i)
    {
        volatile std::size_t value = i;
        for (std::size_t round = 0; round < 256; ++round)
            value = value * 1664525u + 1013904223u;
        (void)value;
    });
    smart::parallel_for(0, 32, workload);
    smart::parallel_for(0, 32, workload);
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
    smart::parallel_for(0, 32, workload);

    bool saw_age_revalidation = false;
    for (const auto& record : smart::nested_execution_trace_snapshot())
        saw_age_revalidation = saw_age_revalidation
            || record.decision_reason == "root_online_revalidation";
    require(saw_age_revalidation,
            "wall-clock plan age did not trigger end-to-end revalidation");
    config.enable_nested_execution_trace = false;
}

void test_thread_pool_reentrant_wait_and_shutdown()
{
    std::atomic<std::size_t> completed{0};
    {
        smart::ThreadPool pool(1);
        std::promise<void> nested_done;
        auto nested_future = nested_done.get_future();
        pool.submit([&]
        {
            for (std::size_t i = 0; i < 32; ++i)
                pool.submit([&] { completed.fetch_add(1, std::memory_order_relaxed); });
            pool.wait();
            nested_done.set_value();
        });
        require(nested_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                "single-worker reentrant ThreadPool wait deadlocked");
        pool.wait();
    }
    require(completed.load(std::memory_order_relaxed) == 32,
            "reentrant ThreadPool wait lost queued work");

    std::atomic<std::size_t> deep_completed{0};
    {
        smart::ThreadPool pool(1);
        std::promise<void> outer_done;
        auto outer_future = outer_done.get_future();
        pool.submit([&]
        {
            pool.submit([&]
            {
                pool.submit([&]
                {
                    deep_completed.fetch_add(1, std::memory_order_relaxed);
                });
                pool.wait();
                deep_completed.fetch_add(1, std::memory_order_relaxed);
            });
            pool.wait();
            deep_completed.fetch_add(1, std::memory_order_relaxed);
            outer_done.set_value();
        });
        require(outer_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                "multi-level reentrant ThreadPool wait deadlocked");
        pool.wait();
    }
    require(deep_completed.load(std::memory_order_relaxed) == 3,
            "multi-level reentrant ThreadPool wait lost stacked work");

    bool rethrew = false;
    {
        smart::ThreadPool pool(2);
        pool.submit([] { throw std::runtime_error("expected unhandled pool job"); });
        try
        {
            pool.wait();
        }
        catch (const std::runtime_error&)
        {
            rethrew = true;
        }
    }
    require(rethrew, "ThreadPool worker exception terminated or was silently lost");

    std::atomic<std::size_t> shutdown_completed{0};
    {
        smart::ThreadPool pool(2);
        for (std::size_t i = 0; i < 64; ++i)
            pool.submit([&] { shutdown_completed.fetch_add(1, std::memory_order_relaxed); });
    }
    require(shutdown_completed.load(std::memory_order_relaxed) == 64,
            "ThreadPool shutdown abandoned queued work");
}

void test_backend_trace_and_nested_exception_contract(smart::ExecutionEngineType engine)
{
    if (engine == smart::ExecutionEngineType::OneTbb
        && !smart::execution_backend_available(engine))
        return;

    auto& config = smart::global_config();
    config.enable_nested_execution_trace = true;
    smart::clear_nested_execution_trace();
    auto session = std::make_shared<smart::NestedExecutionSession>(4, 0xB001);

    smart::NestedExecutionTraceRecord trace;
    trace.root_loop_id = 0xB001;
    trace.loop_id = 0xB002;
    trace.depth = 1;
    trace.iterations = 128;
    trace.parallel = true;
    trace.requested_backend = smart::runtime_name(engine);
    session->begin_trace(trace);

    smart::BackendExecutionRequest request;
    request.total = 128;
    request.concurrency_budget = 4;
    request.chunk_size = 1;
    request.loop_id = trace.loop_id;
    request.nested_session = session;
    std::atomic<std::size_t> visits{0};
    request.function = [&](std::size_t)
    {
        require(session->current_thread_owns_participant(),
                "backend callback did not own its session participant");
        visits.fetch_add(1, std::memory_order_relaxed);
    };
    smart::execution_backend(engine).execute(std::move(request));
    session->finish_trace(trace.loop_id, 0.0, 0, 0.0);

    require(visits.load(std::memory_order_relaxed) == 128,
            "backend execution did not complete exactly once");
    require(session->leased_workers() == 0, "backend success path leaked permits");
    require(session->lease_invariant_violations() == 0,
            "backend success path violated lease invariants");
    const auto records = smart::nested_execution_trace_snapshot();
    require(!records.empty() && records.back().backend_confirmed
                && records.back().backend == smart::runtime_name(engine),
            "trace did not confirm the backend that actually executed");
    require(records.back().leased_workers > 0 && records.back().runtime_concurrency > 0,
            "backend trace omitted lease or runtime concurrency data");

    bool threw = false;
    smart::BackendExecutionRequest failing;
    failing.total = 256;
    failing.concurrency_budget = 4;
    failing.chunk_size = 1;
    failing.loop_id = 0xB003;
    failing.nested_session = session;
    failing.function = [](std::size_t i)
    {
        if (i == 17)
            throw std::runtime_error("expected backend cancellation");
    };
    try
    {
        smart::execution_backend(engine).execute(std::move(failing));
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    require(threw, "backend exception was not propagated");
    require(session->leased_workers() == 0, "backend exception leaked permits");
    require(session->lease_invariant_violations() == 0,
            "backend exception violated lease invariants");
    config.enable_nested_execution_trace = false;
}


void run_deep_nested_work(std::size_t depth,
                          std::size_t maximum_depth,
                          bool should_throw,
                          std::atomic<std::size_t>& leaves)
{
    static constexpr std::array<std::size_t, 5> widths{{3, 4, 3, 5, 7}};
    smart::parallel_for(0, widths[depth], smart::with_parallel_callsite(
        0xC000 + depth,
        [&](std::size_t index)
        {
            if (should_throw && depth == 3 && index == 2)
                throw std::runtime_error("expected deep nested cancellation");
            if (depth == maximum_depth)
                leaves.fetch_add(1, std::memory_order_relaxed);
            else
                run_deep_nested_work(depth + 1, maximum_depth, should_throw, leaves);
        }));
}

void test_deep_nested_cancellation_and_recovery(smart::ExecutionEngineType engine)
{
    if (engine == smart::ExecutionEngineType::OneTbb
        && !smart::execution_backend_available(engine))
        return;

    auto& config = smart::global_config();
    config.execution_engine = engine;
    config.enable_nested_execution_session = true;
    config.enable_nested_root_online_telemetry = true;
    config.enable_nested_online_telemetry = true;
    config.enable_parallel_for_profile_cache = true;
    config.nested_root_concurrency_budget = 4;
    config.enable_nested_execution_trace = true;
    smart::global_function_profile_cache().clear();

    constexpr std::size_t expected_leaves = 3 * 4 * 3 * 5 * 7;
    for (std::size_t repetition = 0; repetition < 24; ++repetition)
    {
        smart::clear_nested_execution_trace();
        std::atomic<std::size_t> interrupted_leaves{0};
        bool threw = false;
        try
        {
            run_deep_nested_work(0, 4, true, interrupted_leaves);
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        require(threw, "deep nested exception did not propagate");

        bool saw_exceptional_trace = false;
        for (const auto& record : smart::nested_execution_trace_snapshot())
            saw_exceptional_trace = saw_exceptional_trace || record.exceptional;
        require(saw_exceptional_trace,
                "deep nested cancellation did not emit an exceptional trace record");

        std::atomic<std::size_t> recovered_leaves{0};
        run_deep_nested_work(0, 4, false, recovered_leaves);
        require(recovered_leaves.load(std::memory_order_relaxed) == expected_leaves,
                "backend did not recover after deep nested cancellation");
    }
    config.enable_nested_execution_trace = false;
}

void test_concurrent_root_progress_under_contention()
{
    auto& config = smart::global_config();
    config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    config.enable_nested_execution_session = true;
    config.enable_parallel_for_profile_cache = true;
    config.nested_root_concurrency_budget = 4;
    smart::global_function_profile_cache().clear();

    constexpr std::size_t root_count = 12;
    std::atomic<bool> release_long_root{false};
    std::atomic<std::size_t> short_roots_completed{0};

    auto long_root = std::async(std::launch::async, [&]
    {
        smart::parallel_for(0, 4, smart::with_parallel_callsite(
            0xD001,
            [&](std::size_t index)
            {
                if (index == 0)
                {
                    while (!release_long_root.load(std::memory_order_acquire))
                        std::this_thread::yield();
                }
                else
                {
                    for (std::size_t spin = 0; spin < 10000; ++spin)
                        std::atomic_signal_fence(std::memory_order_seq_cst);
                }
            }));
    });

    std::vector<std::future<void>> short_roots;
    for (std::size_t root = 0; root < root_count; ++root)
    {
        short_roots.push_back(std::async(std::launch::async, [root, &short_roots_completed]
        {
            std::atomic<std::size_t> visits{0};
            smart::parallel_for(0, 96, smart::with_parallel_callsite(
                0xD100 + root,
                [&](std::size_t)
                {
                    visits.fetch_add(1, std::memory_order_relaxed);
                }));
            require(visits.load(std::memory_order_relaxed) == 96,
                    "concurrent short root lost work");
            short_roots_completed.fetch_add(1, std::memory_order_release);
        }));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (short_roots_completed.load(std::memory_order_acquire) != root_count
           && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    release_long_root.store(true, std::memory_order_release);

    require(short_roots_completed.load(std::memory_order_acquire) == root_count,
            "short roots starved behind an unrelated long root");
    for (auto& root : short_roots)
        root.get();
    require(long_root.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
            "long root did not resume after contention release");
    long_root.get();
}


void test_long_running_cache_churn_and_invalidation()
{
    auto& config = smart::global_config();
    config.parallel_for_profile_cache_max_entries = 64;
    smart::FunctionProfileCache cache;
    const auto profile = profile_with_speedup(2.0);
    smart::ExecutionPlan plan;
    plan.parallel = true;
    plan.engine = smart::ExecutionEngineType::ThreadPool;
    plan.strategy = smart::ExecutionStrategy::DynamicChunks;
    plan.job_count = 4;

    for (std::size_t iteration = 0; iteration < 5000; ++iteration)
    {
        const auto key = key_for(0xE000 + iteration);
        const std::uint64_t epoch = cache.cache_epoch();
        const std::uint64_t generation = cache.store(
            key, profile, 0.25, 1.10, 1, 0, 0.0, iteration + 1, epoch);
        require(generation != 0, "cache churn failed to publish a current observation");
        require(cache.store_stable_plan(key, plan, generation),
                "cache churn rejected a generation-matched stable plan");
        require(cache.size() <= 64, "long-running cache churn exceeded its bound");

        if (iteration != 0 && iteration % 257 == 0)
        {
            cache.clear();
            require(cache.size() == 0, "periodic cache invalidation retained stale profiles");
            require(cache.store(key, profile, 0.25, 1.10, 1, 0, 0.0,
                                iteration + 1, epoch) == 0,
                    "pre-invalidation epoch repopulated long-running cache state");
        }
    }
}

void test_trace_retention_is_bounded()
{
    auto& config = smart::global_config();
    config.nested_execution_trace_max_records = 8;
    smart::clear_nested_execution_trace();
    for (std::uint64_t id = 1; id <= 32; ++id)
    {
        smart::NestedExecutionTraceRecord record;
        record.loop_id = id;
        smart::detail::append_nested_trace_record(std::move(record));
    }
    const auto records = smart::nested_execution_trace_snapshot();
    require(records.size() == 8, "nested trace retention exceeded its configured bound");
    require(records.front().loop_id == 25 && records.back().loop_id == 32,
            "nested trace did not retain the most recent records");
}

void test_scheduler_visible_work_near_size_limit()
{
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    smart::SchedulerVisibleWork work(0, maximum, maximum);
    require(work.total_chunks() == 1,
            "scheduler-visible chunk count overflowed near size_t maximum");
    const auto first = work.try_acquire();
    require(first.valid() && first.begin == 0 && first.end == maximum,
            "scheduler-visible acquisition overflowed near size_t maximum");
    require(!work.try_acquire().valid(),
            "scheduler-visible offset wrapped and duplicated work");
}

void test_static_chunks_use_session_leases_and_release_on_exception()
{
    auto session = std::make_shared<smart::NestedExecutionSession>(4, 700);
    smart::ExecutionContext context;
    context.loop_id = 700;
    context.root_loop_id = 700;
    context.depth = 1;
    context.engine = smart::ExecutionEngineType::StaticThread;
    context.parallel = true;
    context.concurrency_budget = 4;
    context.inherited_concurrency_budget = 4;
    context.nested_session = session;

    smart::ExecutionPlan plan;
    plan.parallel = true;
    plan.engine = smart::ExecutionEngineType::StaticThread;
    plan.strategy = smart::ExecutionStrategy::StaticChunks;
    plan.job_count = 4;

    std::atomic<std::size_t> visits{0};
    smart::execute_workload(
        smart::WorkloadBuilder::index_range(128),
        plan,
        [&](std::size_t)
        {
            require(session->current_thread_owns_participant(),
                    "StaticThread callback bypassed participant ownership");
            visits.fetch_add(1, std::memory_order_relaxed);
        },
        smart::NestedExecutionPolicy::NotNested,
        &context);
    require(visits.load(std::memory_order_relaxed) == 128,
            "StaticThread session execution lost work");
    require(session->maximum_leased_workers() == 4,
            "StaticThread execution did not reserve the planned participant width");
    require(session->leased_workers() == 0, "StaticThread execution leaked permits");

    bool threw = false;
    try
    {
        smart::execute_workload(
            smart::WorkloadBuilder::index_range(128),
            plan,
            [](std::size_t i)
            {
                if (i == 17)
                    throw std::runtime_error("expected static worker exception");
            },
            smart::NestedExecutionPolicy::NotNested,
            &context);
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    require(threw, "StaticThread worker exception was swallowed");
    require(session->leased_workers() == 0,
            "StaticThread exception path leaked session permits");
    require(session->lease_invariant_violations() == 0,
            "StaticThread exception path violated lease invariants");
}

#if SMARTPARALLEL_HAS_TBB
void test_one_tbb_native_delegation_respects_session_width()
{
    auto session = std::make_shared<smart::NestedExecutionSession>(2, 810);
    std::atomic<std::size_t> active{0};
    std::atomic<std::size_t> maximum_active{0};

    tbb::task_arena outer_arena(4);
    outer_arena.execute([&]
    {
        smart::BackendExecutionRequest request;
        request.total = 256;
        request.concurrency_budget = 2;
        request.chunk_size = 1;
        request.native_delegation = true;
        request.nested_session = session;
        request.function = [&](std::size_t)
        {
            const std::size_t now = active.fetch_add(1, std::memory_order_acq_rel) + 1;
            std::size_t observed = maximum_active.load(std::memory_order_relaxed);
            while (observed < now
                   && !maximum_active.compare_exchange_weak(
                       observed, now, std::memory_order_relaxed, std::memory_order_relaxed))
            {
            }
            std::this_thread::yield();
            active.fetch_sub(1, std::memory_order_acq_rel);
        };
        smart::execution_backend(smart::ExecutionEngineType::OneTbb)
            .execute(std::move(request));
    });

    require(maximum_active.load(std::memory_order_relaxed) <= 2,
            "oneTBB native delegation exceeded the leased session width");
    require(session->maximum_leased_workers() <= 2,
            "oneTBB session accounting exceeded the configured budget");
    require(session->leased_workers() == 0,
            "oneTBB native delegation leaked session permits");
    require(session->lease_invariant_violations() == 0,
            "oneTBB native delegation violated lease invariants");
}
#endif

void randomized_tree(std::uint64_t seed,
                     std::size_t depth,
                     std::size_t maximum_depth,
                     std::atomic<std::size_t>& leaves)
{
    const std::uint64_t mixed = seed ^ (depth * 0x9e3779b97f4a7c15ull);
    const std::size_t width = 1 + static_cast<std::size_t>((mixed >> 7) % 7);
    smart::parallel_for(0, width, smart::with_parallel_callsite(
        0x5000 + depth,
        [&](std::size_t i)
        {
            const std::uint64_t branch = mixed ^ ((i + 1) * 0xbf58476d1ce4e5b9ull);
            const bool descend = depth < maximum_depth && (branch % 3 != 0);
            if (descend)
                randomized_tree(seed ^ (i + 0x9e3779b9u), depth + 1, maximum_depth, leaves);
            else
                leaves.fetch_add(1, std::memory_order_relaxed);
        }));
}

void test_randomized_concurrent_roots_and_exception_recovery()
{
    auto& config = smart::global_config();
    config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    config.enable_experience = false;
    config.enable_nested_execution_session = true;
    config.enable_nested_root_online_telemetry = true;
    config.enable_nested_online_telemetry = true;
    config.nested_root_concurrency_budget = 4;
    config.parallel_for_profile_cache_max_entries = 256;
    smart::global_function_profile_cache().clear();

    std::vector<std::future<void>> roots;
    for (std::size_t root = 0; root < 8; ++root)
    {
        roots.push_back(std::async(std::launch::async, [root]
        {
            for (std::size_t repetition = 0; repetition < 40; ++repetition)
            {
                std::atomic<std::size_t> leaves{0};
                randomized_tree(root * 1000 + repetition, 0, 4, leaves);
                require(leaves.load(std::memory_order_relaxed) != 0,
                        "randomized nested tree produced no leaves");

                bool threw = false;
                try
                {
                    smart::parallel_for(0, 32, smart::with_parallel_callsite(
                        0x9000 + root,
                        [repetition](std::size_t i)
                        {
                            if (repetition % 7 == 0 && i == 11)
                                throw std::runtime_error("expected randomized exception");
                        }));
                }
                catch (const std::runtime_error&)
                {
                    threw = true;
                }
                require(threw == (repetition % 7 == 0),
                        "randomized exception propagation was inconsistent");
            }
        }));
    }

    for (auto& root : roots)
    {
        require(root.wait_for(std::chrono::seconds(20)) == std::future_status::ready,
                "randomized concurrent root stress stalled");
        root.get();
    }
    require(smart::global_function_profile_cache().size() <= 256,
            "long-running randomized stress exceeded cache capacity");
}
} // namespace

int main()
{
    try
    {
        ConfigGuard guard;
        auto& config = smart::global_config();
        config.enable_experience = false;
        config.enable_utility_model_runtime = false;
        config.enable_nested_execution_trace = false;

        test_bounded_cache_and_active_entry_protection();
        test_revalidation_is_single_flight();
        test_nested_evidence_decays();
        test_policy_and_explicit_callsite_identity();
        test_stale_plan_generation_and_clear_epoch();
        test_time_based_plan_revalidation();
        test_thread_pool_reentrant_wait_and_shutdown();
        test_backend_trace_and_nested_exception_contract(smart::ExecutionEngineType::ThreadPool);
        test_backend_trace_and_nested_exception_contract(smart::ExecutionEngineType::StaticThread);
        test_deep_nested_cancellation_and_recovery(smart::ExecutionEngineType::ThreadPool);
        test_deep_nested_cancellation_and_recovery(smart::ExecutionEngineType::StaticThread);
#if SMARTPARALLEL_HAS_TBB
        test_backend_trace_and_nested_exception_contract(smart::ExecutionEngineType::OneTbb);
        test_deep_nested_cancellation_and_recovery(smart::ExecutionEngineType::OneTbb);
#endif
        test_concurrent_root_progress_under_contention();
        test_long_running_cache_churn_and_invalidation();
        test_trace_retention_is_bounded();
        test_scheduler_visible_work_near_size_limit();
        test_static_chunks_use_session_leases_and_release_on_exception();
#if SMARTPARALLEL_HAS_TBB
        test_one_tbb_native_delegation_respects_session_width();
#endif
        test_randomized_concurrent_roots_and_exception_recovery();
        std::cout << "nested production stress: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "nested production stress: FAIL: " << error.what() << '\n';
        return 1;
    }
}
