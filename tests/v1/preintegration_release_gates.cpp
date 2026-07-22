#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <smart/core/config.hpp>
#include <smart/decision/exploration_policy.hpp>
#include <smart/execution/backend.hpp>
#include <smart/execution/nested_execution_session.hpp>
#include <smart/execution/thread_pool.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/profiling/function_profile_cache.hpp>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct ConfigGuard
{
    smart::Config saved = smart::global_config();
    ~ConfigGuard() { smart::global_config() = saved; }
};

smart::FunctionProfile reliable_profile()
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
    profile.parallel_worthiness = 2.0;
    profile.metadata.confidence = smart::ObservationConfidence::High;
    return profile;
}

smart::FunctionProfileKey profile_key(std::size_t value)
{
    smart::FunctionProfileKey key;
    key.function_hash = value;
    key.element_size = sizeof(std::size_t);
    key.iteration_bucket = 512;
    key.depth = 1;
    key.concurrency_budget = 4;
    key.engine = smart::ExecutionEngineType::ThreadPool;
    key.policy_signature = 0x165;
    return key;
}

void update_maximum(std::atomic<std::size_t>& maximum, std::size_t value)
{
    std::size_t observed = maximum.load(std::memory_order_relaxed);
    while (observed < value
           && !maximum.compare_exchange_weak(
               observed, value, std::memory_order_relaxed, std::memory_order_relaxed))
    {
    }
}

void test_repeated_backend_cancellation(smart::ExecutionEngineType engine)
{
    if (!smart::execution_backend_available(engine))
        return;

    auto session = std::make_shared<smart::NestedExecutionSession>(4, 0x1650);
    constexpr std::size_t total = 512;
    constexpr std::size_t throw_index = 257;
    bool observed_concurrent_participation = false;

    for (std::size_t repetition = 0; repetition < 32; ++repetition)
    {
        std::vector<std::atomic<unsigned>> visits(total);
        for (auto& visit : visits)
            visit.store(0, std::memory_order_relaxed);
        std::atomic<std::size_t> active{0};
        std::atomic<std::size_t> maximum_active{0};
        std::atomic<std::size_t> started{0};
        std::atomic<bool> duplicated{false};

        smart::BackendExecutionRequest request;
        request.total = total;
        request.concurrency_budget = 4;
        request.chunk_size = 1;
        request.loop_id = 0x1660 + repetition;
        request.nested_session = session;
        request.function = [&](std::size_t index)
        {
            if (visits[index].fetch_add(1, std::memory_order_relaxed) != 0)
                duplicated.store(true, std::memory_order_relaxed);
            started.fetch_add(1, std::memory_order_relaxed);
            const std::size_t now = active.fetch_add(1, std::memory_order_acq_rel) + 1;
            update_maximum(maximum_active, now);
            std::this_thread::sleep_for(std::chrono::microseconds(40));
            active.fetch_sub(1, std::memory_order_acq_rel);
            if (index == throw_index)
                throw std::runtime_error("preintegration cancellation origin");
        };

        bool threw = false;
        try
        {
            smart::execution_backend(engine).execute(std::move(request));
        }
        catch (const std::runtime_error& error)
        {
            threw = std::string(error.what()) == "preintegration cancellation origin";
        }
        require(threw, "originating backend cancellation exception was not propagated");
        require(!duplicated.load(std::memory_order_relaxed),
                "backend cancellation duplicated an iteration");
        require(active.load(std::memory_order_acquire) == 0,
                "backend cancellation returned while callbacks were active");

        const std::size_t started_at_return = started.load(std::memory_order_acquire);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(started.load(std::memory_order_acquire) == started_at_return,
                "backend cancellation abandoned callbacks after returning");
        require(session->leased_workers() == 0,
                "backend cancellation leaked root-session permits");
        require(session->lease_invariant_violations() == 0,
                "backend cancellation violated root-session accounting");
        observed_concurrent_participation = observed_concurrent_participation
            || maximum_active.load(std::memory_order_relaxed) > 1;

        if (engine == smart::ExecutionEngineType::ThreadPool)
        {
            require(smart::global_thread_pool().active_job_count() == 0,
                    "ThreadPool cancellation left active helper jobs");
            require(smart::global_thread_pool().queued_job_count() == 0,
                    "ThreadPool cancellation left queued helper jobs");
        }
    }

    require(observed_concurrent_participation,
            "cancellation stress never exercised concurrent backend participation");

    std::vector<std::atomic<unsigned>> recovery_visits(total);
    for (auto& visit : recovery_visits)
        visit.store(0, std::memory_order_relaxed);
    smart::BackendExecutionRequest recovery;
    recovery.total = total;
    recovery.concurrency_budget = 4;
    recovery.chunk_size = 1;
    recovery.loop_id = 0x16F0;
    recovery.nested_session = session;
    recovery.function = [&](std::size_t index)
    {
        recovery_visits[index].fetch_add(1, std::memory_order_relaxed);
    };
    smart::execution_backend(engine).execute(std::move(recovery));
    for (const auto& visit : recovery_visits)
        require(visit.load(std::memory_order_relaxed) == 1,
                "backend recovery after cancellation was not exactly once");
    require(session->leased_workers() == 0,
            "backend recovery retained root-session permits");
}

void test_profile_cache_strict_bound_under_contention()
{
    auto& config = smart::global_config();
    config.parallel_for_profile_cache_max_entries = 16;
    smart::FunctionProfileCache cache;
    const auto profile = reliable_profile();

    std::vector<std::thread> writers;
    for (std::size_t thread = 0; thread < 8; ++thread)
    {
        writers.emplace_back([thread, &cache, &profile]
        {
            for (std::size_t index = 0; index < 256; ++index)
                cache.store(profile_key(thread * 10000 + index), profile);
        });
    }
    for (auto& writer : writers)
        writer.join();
    require(cache.size() <= 16, "profile cache exceeded its strict configured bound");

    config.parallel_for_profile_cache_max_entries = 2;
    cache.clear();
    cache.store(profile_key(1), profile);
    cache.store(profile_key(2), profile);
    auto first = cache.try_acquire_revalidation(profile_key(1));
    auto second = cache.try_acquire_revalidation(profile_key(2));
    require(first.owns_revalidation() && second.owns_revalidation(),
            "failed to pin profile-cache entries for strict-bound test");
    require(cache.store(profile_key(3), profile) == 0,
            "profile cache published beyond capacity while every entry was active");
    require(cache.size() == 2, "profile cache temporarily exceeded its hard bound");
}

smart::ExecutionPlan experience_plan(std::size_t value)
{
    smart::ExecutionPlan plan;
    plan.parallel = value % 2 != 0;
    plan.engine = value % 3 == 0 ? smart::ExecutionEngineType::StaticThread
                                 : smart::ExecutionEngineType::ThreadPool;
    plan.strategy = plan.parallel ? smart::ExecutionStrategy::DynamicChunks
                                  : smart::ExecutionStrategy::Sequential;
    plan.job_count = 1 + value % 8;
    plan.chunk_size = 1 + value % 32;
    return plan;
}

smart::WorkloadFingerprint experience_fingerprint(std::size_t value)
{
    smart::WorkloadFingerprint fingerprint;
    fingerprint.value = value;
    fingerprint.iteration_bucket = value % 16;
    fingerprint.working_set_bucket = value % 8;
    return fingerprint;
}

void test_experience_and_exploration_bounds()
{
    auto& config = smart::global_config();
    config.experience_cache_max_records = 32;
    config.experience_cache_max_plans_per_record = 4;
    config.online_exploration_state_max_entries = 8;

    smart::ExperienceDatabase database;
    std::vector<std::thread> writers;
    for (std::size_t thread = 0; thread < 8; ++thread)
    {
        writers.emplace_back([thread, &database]
        {
            for (std::size_t index = 0; index < 512; ++index)
            {
                const std::size_t identity = thread * 100000 + index;
                database.record(experience_fingerprint(identity % 128),
                                experience_plan(identity),
                                0.1 + static_cast<double>(identity % 17) * 0.01,
                                0.2);
            }
        });
    }
    for (auto& writer : writers)
        writer.join();

    require(database.size() <= 32, "experience database exceeded its record bound");
    require(database.maximum_plans_in_record() <= 4,
            "experience database exceeded its per-record plan bound");
    require(database.plan_count() <= 32 * 4,
            "experience database aggregate plan retention exceeded its bound");

    const auto newest_fingerprint = experience_fingerprint(0xFFFF0);
    const auto newest_plan = experience_plan(0xFFFF0);
    database.record(newest_fingerprint, newest_plan, 1.0, 1.0);
    require(database.find_plan_copy(newest_fingerprint, newest_plan).has_value(),
            "stale experience entries prevented a new entry from being learned");
    database.clear();
    require(database.size() == 0 && database.plan_count() == 0,
            "experience database clear retained owned cache state");

    smart::OnlineExplorationPolicy exploration;
    smart::PlanCostEstimate sequential;
    sequential.available = true;
    sequential.plan.parallel = false;
    sequential.plan.strategy = smart::ExecutionStrategy::Sequential;
    sequential.ranking_score = 1.0;
    sequential.predicted_total_ms = 1.0;
    sequential.confidence = 1.0;
    smart::PlanCostEstimate parallel = sequential;
    parallel.plan.parallel = true;
    parallel.plan.engine = smart::ExecutionEngineType::ThreadPool;
    parallel.plan.strategy = smart::ExecutionStrategy::DynamicChunks;
    parallel.plan.job_count = 4;
    parallel.ranking_score = 1.01;
    const std::vector<smart::PlanCostEstimate> candidates{sequential, parallel};

    config.enable_online_exploration = false;
    for (std::size_t index = 0; index < 256; ++index)
        exploration.select(experience_fingerprint(index), candidates, sequential.plan, 1.0);
    require(exploration.size() == 0,
            "disabled online exploration accumulated process-lifetime state");

    config.enable_online_exploration = true;
    config.exploration_probability = 1.0;
    config.maximum_exploration_probability = 1.0;
    for (std::size_t index = 0; index < 256; ++index)
        exploration.select(experience_fingerprint(index), candidates, sequential.plan, 1.0);
    require(exploration.size() <= 8, "online exploration state exceeded its configured bound");
    exploration.clear();
    require(exploration.size() == 0, "online exploration clear retained state");
}

void test_trace_and_snapshot_bounds_are_observable()
{
    auto& config = smart::global_config();
    config.nested_plan_snapshot_max_entries = 4;
    auto session = std::make_shared<smart::NestedExecutionSession>(4, 0x1700);
    smart::ExecutionPlan plan;
    for (std::size_t index = 0; index < 32; ++index)
    {
        smart::NestedPlanSnapshotKey key;
        key.function_hash = index;
        key.iteration_bucket = 64;
        key.depth = 2;
        key.concurrency_budget = 4;
        session->store_plan_snapshot(key, plan);
    }
    require(session->plan_snapshot_count() == 4,
            "per-root stable-plan snapshots exceeded their configured bound");
    require(session->pending_trace_count() == 0,
            "new session unexpectedly retained pending trace state");

    config.nested_execution_trace_max_records = 4;
    smart::clear_nested_execution_trace();
    for (std::uint64_t index = 0; index < 32; ++index)
    {
        smart::NestedExecutionTraceRecord record;
        record.loop_id = index;
        smart::detail::append_nested_trace_record(std::move(record));
    }
    const auto trace = smart::nested_execution_trace_snapshot();
    require(trace.size() == 4 && trace.front().loop_id == 28 && trace.back().loop_id == 31,
            "process-wide trace retention was not bounded to the newest records");
}
} // namespace

int main()
{
    try
    {
        ConfigGuard guard;
        auto& config = smart::global_config();
        config.enable_experience = false;
        config.enable_nested_execution_session = true;
        config.nested_root_concurrency_budget = 4;
        config.enable_nested_execution_trace = false;

        test_repeated_backend_cancellation(smart::ExecutionEngineType::ThreadPool);
        test_repeated_backend_cancellation(smart::ExecutionEngineType::StaticThread);
#if SMARTPARALLEL_HAS_TBB
        test_repeated_backend_cancellation(smart::ExecutionEngineType::OneTbb);
#endif
        test_profile_cache_strict_bound_under_contention();
        test_experience_and_exploration_bounds();
        test_trace_and_snapshot_bounds_are_observable();
        std::cout << "pre-integration release gates: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "pre-integration release gates: FAIL: " << error.what() << '\n';
        return 1;
    }
}
