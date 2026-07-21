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
