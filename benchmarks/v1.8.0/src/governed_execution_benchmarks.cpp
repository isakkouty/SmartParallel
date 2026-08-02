#include <smart/execution/parallel.hpp>
#include <smart/runtime/runtime.hpp>
#include <smart/version.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
constexpr std::size_t warmup_count = 2;
constexpr std::uint64_t benchmark_seed = 0x1802026ULL;

std::string csv_escape(const std::string& value)
{
    if (value.find_first_of(",\"\n\r") == std::string::npos)
        return value;
    std::string result = "\"";
    for (char character : value)
    {
        if (character == '\"') result += "\"\"";
        else result += character;
    }
    result += '\"';
    return result;
}

std::string compiler_name()
{
#if defined(_MSC_VER)
    return "MSVC";
#elif defined(__clang__)
    return "Clang";
#elif defined(__GNUC__)
    return "GCC";
#else
    return "unknown";
#endif
}

std::string compiler_version()
{
    std::ostringstream out;
#if defined(_MSC_VER)
    out << _MSC_VER;
#elif defined(__clang__)
    out << __clang_major__ << '.' << __clang_minor__ << '.' << __clang_patchlevel__;
#elif defined(__GNUC__)
    out << __GNUC__ << '.' << __GNUC_MINOR__ << '.' << __GNUC_PATCHLEVEL__;
#else
    out << "unknown";
#endif
    return out.str();
}

std::string operating_system()
{
#if defined(_WIN32)
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "unknown";
#endif
}


void update_maximum(std::atomic<std::size_t>& maximum, std::size_t value)
{
    auto current = maximum.load(std::memory_order_relaxed);
    while (value > current
           && !maximum.compare_exchange_weak(current, value,
                                             std::memory_order_relaxed)) {}
}

std::uint64_t mix(std::uint64_t value) noexcept
{
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31U;
    return value;
}

std::uint64_t burn(std::size_t index, std::size_t rounds)
{
    volatile std::uint64_t value = mix(static_cast<std::uint64_t>(index + 1));
    for (std::size_t round = 0; round < rounds; ++round)
        value = mix(value + round + 0x9e3779b97f4a7c15ULL);
    return value;
}

smart::RuntimeOptions runtime_options(
    const std::shared_ptr<smart::ResourceGovernor>& governor,
    std::size_t ceiling,
    smart::ExecutionEngineType engine = smart::ExecutionEngineType::ThreadPool,
    bool deterministic = false)
{
    smart::RuntimeOptions value;
    value.governor = governor;
    value.maximum_workers = ceiling;
    value.worker_budget = ceiling;
    value.lease_wait_policy = deterministic
        ? smart::LeaseWaitPolicy::FailImmediately : smart::LeaseWaitPolicy::Wait;
    value.execution_mode = deterministic
        ? smart::ExecutionMode::Deterministic : smart::ExecutionMode::Adaptive;
    value.profile_access = smart::ProfileAccess::Disabled;
    value.application_build_identifier = "smartparallel-v180-hardened-benchmark";
    value.build_type = "Release";
    value.scheduler_config.execution_engine = engine;
    value.scheduler_config.enable_parallel_for_auto_profiling = false;
    value.scheduler_config.enable_parallel_for_profile_cache = false;
    value.scheduler_config.enable_parallel_for_backend_calibration = false;
    value.scheduler_config.enable_experience = false;
    value.scheduler_config.enable_nested_execution_session = true;
    value.scheduler_config.enable_nested_parallel_frontier = true;
    value.scheduler_config.nested_min_iterations_per_worker = 1;
    value.scheduler_config.nested_min_parallel_work_ms = 0.0;
    value.scheduler_config.parallel_for_estimated_overhead_ms = 0.0;
    value.scheduler_config.parallel_for_minimum_predicted_speedup = 0.0;
    value.scheduler_config.small_workload_iteration_threshold = 1;
    value.scheduler_config.cheap_workload_sequential_threshold = 0;
    return value;
}

struct Environment
{
    std::string compiler = compiler_name();
    std::string compiler_version = ::compiler_version();
    std::string build_mode = "Release";
    std::string os = operating_system();
    std::string cpu = "effective-capacity-probe";
    smart::EffectiveCpuCapacityReport capacity = smart::effective_cpu_capacity();
};

struct RawRecord
{
    std::string benchmark;
    std::string variant;
    std::string metric;
    std::string unit;
    double value = 0.0;
    std::string operation = "parallel_for";
    std::string workload = "";
    std::string scheduler = "";
    std::string numerical_policy = "not_applicable";
    std::string execution_mode = "Adaptive";
    smart::ResourceDecisionReport report{};
    std::size_t declared_budget = 0;
    std::size_t runtime_ceiling = 0;
    bool correctness = true;
    std::string output_digest = "";
    double duration_ms = 0.0;
    std::size_t repetition = 0;
    std::string measurement_order = "single";
    std::size_t operations_completed = 1;
    std::size_t peak_participation = 0;
    std::uint64_t bypass_count = 0;
    double oldest_waiter_us = 0.0;
    std::string cancellation_state = "not_requested";
};

class RawWriter
{
  public:
    RawWriter(const std::filesystem::path& path, Environment environment)
        : output_(path), environment_(std::move(environment))
    {
        if (!output_) throw std::runtime_error("cannot create v1.8 raw evidence");
        output_ << "schema_version,smartparallel_version,compiler,compiler_version,build_mode,"
                   "operating_system,cpu_identity,effective_cpu_capacity,effective_capacity_reliable,"
                   "effective_capacity_source,declared_governor_budget,runtime_ceiling,benchmark,variant,"
                   "metric,unit,value,operation,workload_dimensions,scheduler,numerical_policy,execution_mode,"
                   "requested_workers,minimum_workers,preferred_workers,maximum_workers,granted_workers,"
                   "scheduler_cap,observed_participating_workers,nesting_depth,nested_mode,exact_grant_requirement,"
                   "admission_policy,admission_result,wait_duration_us,cancellation_state,correctness,output_digest,"
                   "duration_ms,repetition_index,measurement_order,warmup_count,randomization_seed,operations_completed,"
                   "peak_participation,bounded_bypass_count,oldest_waiter_us\n";
    }

    void write(const RawRecord& row)
    {
        const auto& report = row.report;
        output_ << "2,1.8.0," << csv_escape(environment_.compiler) << ','
                << csv_escape(environment_.compiler_version) << ','
                << environment_.build_mode << ',' << environment_.os << ','
                << csv_escape(environment_.cpu) << ',' << environment_.capacity.capacity << ','
                << (environment_.capacity.reliable ? 1 : 0) << ','
                << csv_escape(environment_.capacity.source) << ','
                << row.declared_budget << ',' << row.runtime_ceiling << ','
                << csv_escape(row.benchmark) << ',' << csv_escape(row.variant) << ','
                << csv_escape(row.metric) << ',' << csv_escape(row.unit) << ','
                << std::setprecision(17) << row.value << ','
                << csv_escape(row.operation) << ',' << csv_escape(row.workload) << ','
                << csv_escape(row.scheduler.empty() ? report.scheduler : row.scheduler) << ','
                << csv_escape(row.numerical_policy) << ',' << csv_escape(row.execution_mode) << ','
                << report.requested_workers << ',' << report.minimum_workers << ','
                << report.preferred_workers << ',' << report.maximum_workers << ','
                << report.granted_workers << ',' << report.scheduler_concurrency_cap << ','
                << report.observed_participating_threads << ',' << report.nesting_depth << ','
                << smart::nested_lease_mode_name(report.nested_mode) << ','
                << (report.exact_grant_required ? 1 : 0) << ','
                << smart::lease_wait_policy_name(report.wait_policy) << ','
                << smart::lease_acquire_status_name(report.admission_status) << ','
                << std::chrono::duration<double, std::micro>(report.wait_duration).count() << ','
                << row.cancellation_state << ',' << (row.correctness ? 1 : 0) << ','
                << csv_escape(row.output_digest) << ',' << row.duration_ms << ','
                << row.repetition << ',' << csv_escape(row.measurement_order) << ','
                << warmup_count << ',' << benchmark_seed << ',' << row.operations_completed << ','
                << row.peak_participation << ',' << row.bypass_count << ','
                << row.oldest_waiter_us << '\n';
    }

  private:
    std::ofstream output_;
    Environment environment_;
};

smart::ResourceDecisionReport report_from_acquire(
    const smart::LeaseRequest& request,
    const smart::LeaseAcquireResult& result,
    std::size_t budget)
{
    smart::ResourceDecisionReport report;
    report.operation_identity = request.operation_identity;
    report.process_cpu_budget = budget;
    report.runtime_worker_ceiling = request.maximum_workers;
    report.requested_workers = request.requested_workers;
    report.minimum_workers = request.minimum_workers;
    report.preferred_workers = request.preferred_workers == 0
        ? request.requested_workers : request.preferred_workers;
    report.maximum_workers = request.maximum_workers == 0
        ? request.requested_workers : request.maximum_workers;
    report.granted_workers = result.granted_workers;
    report.scheduler_concurrency_cap = result.granted_workers;
    report.observed_participating_threads = result.granted_workers;
    report.exact_grant_required = request.exact_grant_required;
    report.wait_policy = request.wait_policy;
    report.admission_status = result.status;
    report.wait_duration = result.wait_duration;
    return report;
}

struct ConcurrentRun
{
    double elapsed_ms = 0.0;
    std::vector<double> runtime_ms;
    std::vector<smart::ResourceDecisionReport> reports;
    std::size_t peak = 0;
    std::uint64_t digest = 0;
    bool correct = true;
    std::uint64_t bypasses = 0;
    double oldest_waiter_us = 0.0;
};

ConcurrentRun run_concurrent(bool governed,
                             std::size_t runtime_count,
                             std::size_t declared_budget,
                             std::size_t runtime_ceiling,
                             std::size_t items,
                             std::size_t spin_rounds)
{
    auto shared = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{declared_budget, 4,
                                       std::chrono::milliseconds(10)});
    std::vector<std::shared_ptr<smart::ResourceGovernor>> governors;
    std::vector<std::unique_ptr<smart::Runtime>> runtimes;
    governors.reserve(runtime_count);
    runtimes.reserve(runtime_count);
    for (std::size_t index = 0; index < runtime_count; ++index)
    {
        auto governor = governed ? shared : std::make_shared<smart::ResourceGovernor>(
            smart::ResourceGovernorOptions{runtime_ceiling, 4,
                                           std::chrono::milliseconds(10)});
        governors.push_back(governor);
        runtimes.push_back(std::make_unique<smart::Runtime>(
            runtime_options(governor, runtime_ceiling)));
    }

    std::atomic<bool> start{false};
    std::atomic<std::size_t> active{0};
    std::atomic<std::size_t> maximum{0};
    std::atomic<std::uint64_t> digest{0};
    std::vector<double> durations(runtime_count, 0.0);
    std::vector<std::thread> threads;
    threads.reserve(runtime_count);
    const auto batch_start = Clock::now();
    for (std::size_t runtime_index = 0; runtime_index < runtime_count; ++runtime_index)
    {
        threads.emplace_back([&, runtime_index]
        {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            const auto begin = Clock::now();
            std::atomic<std::uint64_t> local{0};
            smart::parallel_for(runtimes[runtime_index]->context(),
                std::size_t{0}, items, [&](std::size_t item)
                {
                    const std::size_t current = active.fetch_add(
                        1, std::memory_order_acq_rel) + 1;
                    update_maximum(maximum, current);
                    local.fetch_xor(burn(item + runtime_index * items, spin_rounds),
                                    std::memory_order_relaxed);
                    active.fetch_sub(1, std::memory_order_acq_rel);
                });
            durations[runtime_index] = std::chrono::duration<double, std::milli>(
                Clock::now() - begin).count();
            digest.fetch_xor(local.load(std::memory_order_relaxed),
                             std::memory_order_relaxed);
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();
    const double elapsed = std::chrono::duration<double, std::milli>(
        Clock::now() - batch_start).count();

    ConcurrentRun result;
    result.elapsed_ms = elapsed;
    result.runtime_ms = durations;
    result.peak = maximum.load(std::memory_order_relaxed);
    result.digest = digest.load(std::memory_order_relaxed);
    result.correct = active.load(std::memory_order_relaxed) == 0;
    for (const auto& runtime : runtimes)
        result.reports.push_back(runtime->last_resource_decision_report());
    for (const auto& governor : governors)
    {
        const auto snapshot = governor->snapshot();
        result.bypasses += snapshot.total_bypasses;
        result.oldest_waiter_us = std::max(
            result.oldest_waiter_us,
            static_cast<double>(snapshot.oldest_waiter_age_ns) / 1000.0);
        result.correct = result.correct && snapshot.active_permits == 0;
    }
    if (governed)
        result.correct = result.correct && result.peak <= declared_budget;
    return result;
}

void warmup_concurrent(std::size_t budget)
{
    for (std::size_t iteration = 0; iteration < warmup_count; ++iteration)
    {
        (void)run_concurrent(true, 2, budget, budget, budget * 4, 80);
        (void)run_concurrent(false, 2, budget, budget, budget * 4, 80);
    }
}

void write_concurrent_records(RawWriter& writer,
                              const ConcurrentRun& result,
                              const std::string& variant,
                              std::size_t repetition,
                              const std::string& order,
                              std::size_t budget,
                              std::size_t ceiling,
                              std::size_t runtime_count,
                              std::size_t items)
{
    const double throughput = result.elapsed_ms > 0.0
        ? static_cast<double>(runtime_count) / (result.elapsed_ms / 1000.0) : 0.0;
    const auto report = result.reports.empty()
        ? smart::ResourceDecisionReport{} : result.reports.front();
    const std::string workload = "runtimes=" + std::to_string(runtime_count)
        + ";items_per_runtime=" + std::to_string(items);
    RawRecord wall;
    wall.benchmark = "governed_vs_ungoverned";
    wall.variant = variant;
    wall.metric = "batch_wall_time";
    wall.unit = "ms";
    wall.value = result.elapsed_ms;
    wall.duration_ms = result.elapsed_ms;
    wall.report = report;
    wall.declared_budget = budget;
    wall.runtime_ceiling = ceiling;
    wall.correctness = result.correct;
    wall.output_digest = std::to_string(result.digest);
    wall.repetition = repetition;
    wall.measurement_order = order;
    wall.operations_completed = runtime_count;
    wall.peak_participation = result.peak;
    wall.bypass_count = result.bypasses;
    wall.oldest_waiter_us = result.oldest_waiter_us;
    wall.workload = workload;
    writer.write(wall);

    RawRecord throughput_row = wall;
    throughput_row.metric = "throughput";
    throughput_row.unit = "operations_per_second";
    throughput_row.value = throughput;
    writer.write(throughput_row);

    RawRecord participation = wall;
    participation.metric = "peak_participation";
    participation.unit = "threads";
    participation.value = static_cast<double>(result.peak);
    writer.write(participation);

    for (std::size_t index = 0; index < result.runtime_ms.size(); ++index)
    {
        RawRecord latency = wall;
        latency.benchmark = "runtime_latency";
        latency.variant = variant + "_runtime_" + std::to_string(index);
        latency.metric = "completion_latency";
        latency.unit = "ms";
        latency.value = result.runtime_ms[index];
        latency.duration_ms = result.runtime_ms[index];
        if (index < result.reports.size()) latency.report = result.reports[index];
        latency.operations_completed = 1;
        writer.write(latency);
    }
}

void benchmark_governor_overhead(RawWriter& writer,
                                 std::size_t repetition,
                                 std::size_t budget)
{
    constexpr std::size_t batch = 1000;
    auto governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{budget, 4, std::chrono::milliseconds(10)});

    smart::LeaseRequest exact;
    exact.requested_workers = 1;
    exact.minimum_workers = 1;
    exact.preferred_workers = 1;
    exact.maximum_workers = 1;
    exact.exact_grant_required = true;
    exact.operation_identity = "overhead_exact";
    const auto start = Clock::now();
    smart::LeaseAcquireResult last;
    for (std::size_t index = 0; index < batch; ++index)
        last = governor->acquire(exact);
    const double exact_us = std::chrono::duration<double, std::micro>(
        Clock::now() - start).count() / static_cast<double>(batch);
    RawRecord row;
    row.benchmark = "governor_overhead";
    row.variant = "uncontended_exact_acquire_release";
    row.metric = "latency";
    row.unit = "us_per_acquire_release";
    row.value = exact_us;
    row.duration_ms = exact_us / 1000.0;
    row.report = report_from_acquire(exact, last, budget);
    row.declared_budget = budget;
    row.runtime_ceiling = 1;
    row.repetition = repetition;
    row.operations_completed = batch;
    row.peak_participation = 1;
    writer.write(row);
    last.lease = {};

    smart::LeaseRequest root_request = exact;
    root_request.requested_workers = budget;
    root_request.minimum_workers = budget;
    root_request.preferred_workers = budget;
    root_request.maximum_workers = budget;
    auto root = governor->acquire(root_request);
    if (!root) throw std::runtime_error("nested overhead root acquisition failed");
    const auto nested_start = Clock::now();
    for (std::size_t index = 0; index < batch; ++index)
    {
        auto child = root.lease.inherit(1);
        if (!child) throw std::runtime_error("inherited lease benchmark failed");
    }
    const double inherit_us = std::chrono::duration<double, std::micro>(
        Clock::now() - nested_start).count() / static_cast<double>(batch);
    RawRecord inherited = row;
    inherited.variant = "inherited_nested_lease";
    inherited.value = inherit_us;
    inherited.report.nested_mode = smart::NestedLeaseMode::ReuseParent;
    inherited.report.nesting_depth = 1;
    inherited.report.granted_workers = 1;
    inherited.report.scheduler_concurrency_cap = 1;
    inherited.report.observed_participating_threads = 1;
    writer.write(inherited);
    root.lease = {};

    if (budget > 1)
    {
        auto blocker = governor->acquire([&]
        {
            smart::LeaseRequest request;
            request.requested_workers = budget - 1;
            request.minimum_workers = budget - 1;
            request.preferred_workers = budget - 1;
            request.maximum_workers = budget - 1;
            request.exact_grant_required = true;
            return request;
        }());
        smart::LeaseRequest flexible;
        flexible.requested_workers = budget;
        flexible.minimum_workers = 1;
        flexible.preferred_workers = budget;
        flexible.maximum_workers = budget;
        flexible.operation_identity = "overhead_partial";
        const auto partial_start = Clock::now();
        auto partial = governor->acquire(flexible);
        const double partial_us = std::chrono::duration<double, std::micro>(
            Clock::now() - partial_start).count();
        RawRecord partial_row = row;
        partial_row.variant = "flexible_partial_grant";
        partial_row.value = partial_us;
        partial_row.report = report_from_acquire(flexible, partial, budget);
        partial_row.runtime_ceiling = budget;
        partial_row.operations_completed = 1;
        writer.write(partial_row);
        partial.lease = {};

        smart::LeaseRequest impossible_now = flexible;
        impossible_now.minimum_workers = budget;
        impossible_now.exact_grant_required = true;
        impossible_now.operation_identity = "overhead_immediate_failure";
        const auto fail_start = Clock::now();
        auto failed = governor->acquire(impossible_now);
        const double fail_us = std::chrono::duration<double, std::micro>(
            Clock::now() - fail_start).count();
        RawRecord fail_row = row;
        fail_row.variant = "immediate_failure";
        fail_row.value = fail_us;
        fail_row.correctness = failed.status == smart::LeaseAcquireStatus::WouldBlock;
        fail_row.report = report_from_acquire(impossible_now, failed, budget);
        fail_row.runtime_ceiling = budget;
        fail_row.operations_completed = 1;
        writer.write(fail_row);
        blocker.lease = {};
    }
}

void benchmark_cancellation(RawWriter& writer,
                            std::size_t repetition,
                            std::size_t budget)
{
    smart::ResourceGovernor governor({budget, 4, std::chrono::milliseconds(10)});
    smart::LeaseRequest hold_request;
    hold_request.requested_workers = budget;
    hold_request.minimum_workers = budget;
    hold_request.preferred_workers = budget;
    hold_request.maximum_workers = budget;
    hold_request.exact_grant_required = true;
    auto held = governor.acquire(hold_request);
    smart::CancellationSource source;
    smart::LeaseRequest waiting = hold_request;
    waiting.wait_policy = smart::LeaseWaitPolicy::Wait;
    waiting.cancellation = source.token();
    waiting.operation_identity = "cancellation_notification";
    smart::LeaseAcquireResult result;
    std::thread waiter([&] { result = governor.acquire(waiting); });
    while (governor.snapshot().pending_requests == 0) std::this_thread::yield();
    const auto start = Clock::now();
    source.request_cancellation();
    waiter.join();
    const double latency_us = std::chrono::duration<double, std::micro>(
        Clock::now() - start).count();
    RawRecord row;
    row.benchmark = "governor_overhead";
    row.variant = "direct_cancellation_notification";
    row.metric = "latency";
    row.unit = "us";
    row.value = latency_us;
    row.duration_ms = latency_us / 1000.0;
    row.report = report_from_acquire(waiting, result, budget);
    row.declared_budget = budget;
    row.runtime_ceiling = budget;
    row.correctness = result.status == smart::LeaseAcquireStatus::Cancelled;
    row.cancellation_state = "requested";
    row.repetition = repetition;
    writer.write(row);
}

void benchmark_admission_fairness(RawWriter& writer,
                                  std::size_t repetition,
                                  std::size_t budget)
{
    if (budget < 2) return;

    constexpr std::size_t bypass_limit = 4;
    constexpr std::size_t small_request_count = 8;
    auto governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{
            budget, bypass_limit, std::chrono::milliseconds(25)});

    smart::LeaseRequest holder_request;
    holder_request.requested_workers = budget - 1;
    holder_request.minimum_workers = budget - 1;
    holder_request.preferred_workers = budget - 1;
    holder_request.maximum_workers = budget - 1;
    holder_request.exact_grant_required = true;
    holder_request.wait_policy = smart::LeaseWaitPolicy::FailImmediately;
    holder_request.operation_identity = "fairness_holder";
    auto holder = governor->acquire(holder_request);
    if (!holder) throw std::runtime_error("fairness holder acquisition failed");

    struct Outcome
    {
        smart::LeaseRequest request;
        smart::LeaseAcquireResult result;
        std::size_t completion_rank = 0;
    };
    Outcome large;
    large.request.requested_workers = budget;
    large.request.minimum_workers = budget;
    large.request.preferred_workers = budget;
    large.request.maximum_workers = budget;
    large.request.exact_grant_required = true;
    large.request.wait_policy = smart::LeaseWaitPolicy::Wait;
    large.request.operation_identity = "fairness_large_exact";

    std::vector<Outcome> small(small_request_count);
    for (std::size_t index = 0; index < small.size(); ++index)
    {
        auto& request = small[index].request;
        request.requested_workers = 1;
        request.minimum_workers = 1;
        request.preferred_workers = 1;
        request.maximum_workers = 1;
        request.exact_grant_required = true;
        request.wait_policy = smart::LeaseWaitPolicy::Wait;
        request.operation_identity = "fairness_small_" + std::to_string(index);
    }

    std::atomic<std::size_t> completion_counter{0};
    std::thread large_thread([&]
    {
        large.result = governor->acquire(large.request);
        if (large.result)
        {
            large.completion_rank = completion_counter.fetch_add(
                1, std::memory_order_acq_rel) + 1;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            large.result.lease = {};
        }
    });

    const auto enqueue_deadline = Clock::now() + std::chrono::seconds(2);
    while (governor->snapshot().pending_requests < 1
           && Clock::now() < enqueue_deadline)
        std::this_thread::yield();
    if (governor->snapshot().pending_requests < 1)
    {
        holder.lease = {};
        large_thread.join();
        throw std::runtime_error("large fairness request did not enter the queue");
    }

    std::vector<std::thread> small_threads;
    small_threads.reserve(small.size());
    for (std::size_t index = 0; index < small.size(); ++index)
    {
        small_threads.emplace_back([&, index]
        {
            auto& outcome = small[index];
            outcome.result = governor->acquire(outcome.request);
            if (outcome.result)
            {
                outcome.completion_rank = completion_counter.fetch_add(
                    1, std::memory_order_acq_rel) + 1;
                outcome.result.lease = {};
            }
        });
    }

    const auto reservation_deadline = Clock::now() + std::chrono::seconds(2);
    while (governor->snapshot().total_oldest_reservations == 0
           && Clock::now() < reservation_deadline)
        std::this_thread::yield();

    const auto before_release = governor->snapshot();
    holder.lease = {};
    for (auto& thread : small_threads) thread.join();
    large_thread.join();
    const auto final_snapshot = governor->snapshot();

    const bool all_small_granted = std::all_of(
        small.begin(), small.end(), [](const Outcome& outcome)
        {
            return outcome.result.status == smart::LeaseAcquireStatus::Granted
                && outcome.completion_rank != 0;
        });
    const bool correct = large.result.status == smart::LeaseAcquireStatus::Granted
        && large.completion_rank > 0
        && large.completion_rank <= bypass_limit + 1
        && all_small_granted
        && before_release.total_oldest_reservations >= 1
        && final_snapshot.total_bypasses <= bypass_limit
        && final_snapshot.active_permits == 0
        && final_snapshot.pending_requests == 0;

    RawRecord large_row;
    large_row.benchmark = "admission_fairness";
    large_row.variant = "large_exact_request";
    large_row.metric = "completion_rank";
    large_row.unit = "rank";
    large_row.value = static_cast<double>(large.completion_rank);
    large_row.report = report_from_acquire(large.request, large.result, budget);
    large_row.declared_budget = budget;
    large_row.runtime_ceiling = budget;
    large_row.correctness = correct;
    large_row.repetition = repetition;
    large_row.bypass_count = final_snapshot.total_bypasses;
    large_row.oldest_waiter_us = std::chrono::duration<double, std::micro>(
        large.result.wait_duration).count();
    large_row.workload = "one_large_exact_plus_eight_small_exact";
    writer.write(large_row);

    for (std::size_t index = 0; index < small.size(); ++index)
    {
        RawRecord row = large_row;
        row.variant = "small_request_" + std::to_string(index);
        row.value = static_cast<double>(small[index].completion_rank);
        row.report = report_from_acquire(small[index].request,
                                         small[index].result, budget);
        row.oldest_waiter_us = std::chrono::duration<double, std::micro>(
            small[index].result.wait_duration).count();
        writer.write(row);
    }
}

void benchmark_partial_admission(RawWriter& writer,
                                 std::size_t repetition,
                                 std::size_t budget)
{
    if (budget <= 1) return;
    auto governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{budget, 4, std::chrono::milliseconds(10)});
    smart::LeaseRequest hold;
    hold.requested_workers = budget - 1;
    hold.minimum_workers = budget - 1;
    hold.preferred_workers = budget - 1;
    hold.maximum_workers = budget - 1;
    hold.exact_grant_required = true;
    auto blocker = governor->acquire(hold);
    smart::Runtime runtime(runtime_options(governor, budget));
    std::atomic<std::size_t> visits{0};
    const auto start = Clock::now();
    smart::parallel_for(runtime.context(), std::size_t{0}, budget * 64,
        [&](std::size_t index)
        {
            (void)burn(index, 80);
            visits.fetch_add(1, std::memory_order_relaxed);
        });
    const double elapsed = std::chrono::duration<double, std::milli>(
        Clock::now() - start).count();
    const auto report = runtime.last_resource_decision_report();
    RawRecord row;
    row.benchmark = "adaptive_partial_grant";
    row.variant = "available_one_of_preferred_budget";
    row.metric = "granted_workers";
    row.unit = "threads";
    row.value = static_cast<double>(report.granted_workers);
    row.duration_ms = elapsed;
    row.report = report;
    row.declared_budget = budget;
    row.runtime_ceiling = budget;
    row.correctness = visits.load() == budget * 64
        && report.minimum_workers == 1
        && report.preferred_workers == budget
        && report.maximum_workers == budget
        && report.granted_workers == 1;
    row.repetition = repetition;
    row.peak_participation = report.observed_participating_threads;
    writer.write(row);
}

void nested_recursive(std::size_t remaining,
                      std::atomic<std::size_t>& active,
                      std::atomic<std::size_t>& maximum,
                      std::atomic<std::size_t>& leaves)
{
    if (remaining == 0)
    {
        const auto current = active.fetch_add(1) + 1;
        update_maximum(maximum, current);
        (void)burn(leaves.fetch_add(1), 80);
        active.fetch_sub(1);
        return;
    }
    smart::parallel_for(std::size_t{0}, std::size_t{2}, [&](std::size_t)
    {
        nested_recursive(remaining - 1, active, maximum, leaves);
    });
}

void benchmark_nested(RawWriter& writer,
                      std::size_t repetition,
                      std::size_t budget)
{
    for (std::size_t depth = 1; depth <= 4; ++depth)
    {
        auto governor = std::make_shared<smart::ResourceGovernor>(
            smart::ResourceGovernorOptions{budget, 4, std::chrono::milliseconds(10)});
        smart::Runtime runtime(runtime_options(governor, budget));
        std::atomic<std::size_t> active{0};
        std::atomic<std::size_t> maximum{0};
        std::atomic<std::size_t> leaves{0};
        const auto start = Clock::now();
        smart::parallel_for(runtime.context(), std::size_t{0}, std::size_t{2},
            [&](std::size_t)
            {
                nested_recursive(depth - 1, active, maximum, leaves);
            });
        const double elapsed = std::chrono::duration<double, std::milli>(
            Clock::now() - start).count();
        const auto report = runtime.last_resource_decision_report();
        RawRecord row;
        row.benchmark = "nested_execution";
        row.variant = "depth_" + std::to_string(depth);
        row.metric = "peak_participation";
        row.unit = "threads";
        row.value = static_cast<double>(maximum.load());
        row.duration_ms = elapsed;
        row.report = report;
        row.declared_budget = budget;
        row.runtime_ceiling = budget;
        row.correctness = leaves.load() == (std::size_t{1} << depth)
            && maximum.load() <= report.granted_workers
            && governor->snapshot().total_grants == 1;
        row.repetition = repetition;
        row.peak_participation = maximum.load();
        row.workload = "depth=" + std::to_string(depth)
            + ";leaves=" + std::to_string(leaves.load());
        writer.write(row);
    }
}

void benchmark_scheduler(RawWriter& writer,
                         std::size_t repetition,
                         std::size_t budget,
                         smart::ExecutionEngineType engine,
                         const std::string& label)
{
    const std::size_t ceiling = label == "sequential" ? 1 : budget;
    auto governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{ceiling, 4, std::chrono::milliseconds(10)});
    smart::Runtime runtime(runtime_options(governor, ceiling, engine));
    constexpr std::size_t items = 512;
    std::atomic<std::uint64_t> digest{0};
    const auto start = Clock::now();
    smart::parallel_for(runtime.context(), std::size_t{0}, items,
        [&](std::size_t index)
        {
            digest.fetch_xor(burn(index, 200), std::memory_order_relaxed);
        });
    const double elapsed = std::chrono::duration<double, std::milli>(
        Clock::now() - start).count();
    const auto report = runtime.last_resource_decision_report();
    RawRecord row;
    row.benchmark = "scheduler_comparison";
    row.variant = label;
    row.metric = "duration";
    row.unit = "ms";
    row.value = elapsed;
    row.duration_ms = elapsed;
    row.report = report;
    row.declared_budget = ceiling;
    row.runtime_ceiling = ceiling;
    row.correctness = report.observed_participating_threads <= report.scheduler_concurrency_cap
        && report.scheduler_concurrency_cap <= report.granted_workers;
    row.output_digest = std::to_string(digest.load());
    row.repetition = repetition;
    row.peak_participation = report.observed_participating_threads;
    row.scheduler = label;
    writer.write(row);
}

void benchmark_deterministic(RawWriter& writer,
                             std::size_t repetition,
                             std::size_t budget)
{
    auto governor = std::make_shared<smart::ResourceGovernor>(
        smart::ResourceGovernorOptions{budget, 4, std::chrono::milliseconds(10)});
    smart::Runtime success(runtime_options(
        governor, budget, smart::ExecutionEngineType::ThreadPool, true));
    std::vector<int> output(budget * 64, 0);
    smart::parallel_for(success.context(), std::size_t{0}, output.size(),
        [&](std::size_t index) { output[index] = static_cast<int>(index + 1); });
    const auto success_report = success.last_resource_decision_report();
    RawRecord success_row;
    success_row.benchmark = "deterministic_exact_grant";
    success_row.variant = "success";
    success_row.metric = "accepted";
    success_row.unit = "boolean";
    success_row.value = 1.0;
    success_row.report = success_report;
    success_row.declared_budget = budget;
    success_row.runtime_ceiling = budget;
    success_row.execution_mode = "Deterministic";
    success_row.correctness = success_report.exact_grant_required
        && success_report.granted_workers == success_report.requested_workers;
    success_row.repetition = repetition;
    writer.write(success_row);

    smart::LeaseRequest hold;
    hold.requested_workers = 1;
    hold.minimum_workers = 1;
    hold.preferred_workers = 1;
    hold.maximum_workers = 1;
    hold.exact_grant_required = true;
    auto blocker = governor->acquire(hold);
    smart::Runtime failure(runtime_options(
        governor, budget, smart::ExecutionEngineType::ThreadPool, true));
    std::vector<int> unchanged(budget * 64, 7);
    const auto before = unchanged;
    bool rejected = false;
    try
    {
        smart::parallel_for(failure.context(), std::size_t{0}, unchanged.size(),
            [&](std::size_t index) { unchanged[index] = 9; });
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    const auto failure_report = failure.last_resource_decision_report();
    RawRecord failure_row = success_row;
    failure_row.variant = "insufficient_budget_failure";
    failure_row.value = rejected ? 1.0 : 0.0;
    failure_row.report = failure_report;
    failure_row.correctness = rejected && unchanged == before
        && failure_report.admission_status == smart::LeaseAcquireStatus::WouldBlock;
    writer.write(failure_row);
}

void write_environment(const std::filesystem::path& path,
                       const Environment& environment,
                       std::size_t repetitions)
{
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot create v1.8 environment record");
    out << "SmartParallel version: 1.8.0\n"
        << "Compiler: " << environment.compiler << ' ' << environment.compiler_version << "\n"
        << "Build mode: " << environment.build_mode << "\n"
        << "Operating system: " << environment.os << "\n"
        << "Effective CPU capacity: " << environment.capacity.capacity << "\n"
        << "Effective capacity source: " << environment.capacity.source << "\n"
        << "Effective capacity reliable: " << (environment.capacity.reliable ? "yes" : "no") << "\n"
        << "Effective capacity diagnostic: " << environment.capacity.diagnostic << "\n"
        << "Repetitions: " << repetitions << "\n"
        << "Warmups per paired scenario: " << warmup_count << "\n"
        << "Randomization seed: " << benchmark_seed << "\n"
        << "Pair ordering: deterministic alternating governed/ungoverned\n";
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::filesystem::path output = argc > 1
            ? std::filesystem::path(argv[1]) : std::filesystem::path("v180_benchmark_results");
        const std::size_t repetitions = argc > 2
            ? static_cast<std::size_t>(std::stoull(argv[2])) : std::size_t{11};
        if (repetitions < 3) throw std::invalid_argument("at least three repetitions are required");
        std::filesystem::create_directories(output);
        Environment environment;
        environment.cpu = "effective_cpu_capacity=" + std::to_string(environment.capacity.capacity);
        const std::size_t effective = std::max<std::size_t>(1, environment.capacity.capacity);
        const std::size_t full_budget = std::min<std::size_t>(effective, 16);
        const std::size_t concurrency_budget = std::max<std::size_t>(2, full_budget);
        RawWriter writer(output / "raw.csv", environment);
        write_environment(output / "environment.txt", environment, repetitions);

        warmup_concurrent(concurrency_budget);
        for (std::size_t repetition = 0; repetition < repetitions; ++repetition)
        {
            benchmark_governor_overhead(writer, repetition, concurrency_budget);
            benchmark_cancellation(writer, repetition, concurrency_budget);
            benchmark_admission_fairness(writer, repetition, concurrency_budget);
            benchmark_partial_admission(writer, repetition, concurrency_budget);
            benchmark_nested(writer, repetition, concurrency_budget);
            benchmark_scheduler(writer, repetition, concurrency_budget,
                                smart::ExecutionEngineType::Auto, "sequential");
            benchmark_scheduler(writer, repetition, concurrency_budget,
                                smart::ExecutionEngineType::ThreadPool, "thread_pool");
            benchmark_scheduler(writer, repetition, concurrency_budget,
                                smart::ExecutionEngineType::StaticThread, "static_thread");
#if SMARTPARALLEL_HAS_TBB
            benchmark_scheduler(writer, repetition, concurrency_budget,
                                smart::ExecutionEngineType::OneTbb, "one_tbb");
#endif
            benchmark_deterministic(writer, repetition, concurrency_budget);

            const bool governed_first = (repetition % 2) == 0;
            ConcurrentRun governed;
            ConcurrentRun ungoverned;
            const std::size_t pressure_runtimes = full_budget > 1 ? 2 : 1;
            const std::size_t pressure_items = std::max<std::size_t>(64, full_budget * 32);
            if (governed_first)
            {
                governed = run_concurrent(true, pressure_runtimes, full_budget,
                                          full_budget, pressure_items, 6000);
                ungoverned = run_concurrent(false, pressure_runtimes, full_budget,
                                            full_budget, pressure_items, 6000);
            }
            else
            {
                ungoverned = run_concurrent(false, pressure_runtimes, full_budget,
                                            full_budget, pressure_items, 6000);
                governed = run_concurrent(true, pressure_runtimes, full_budget,
                                          full_budget, pressure_items, 6000);
            }
            const std::string order = governed_first
                ? "governed_then_ungoverned" : "ungoverned_then_governed";
            write_concurrent_records(writer, governed, "governed", repetition, order,
                                     full_budget, full_budget, pressure_runtimes, pressure_items);
            write_concurrent_records(writer, ungoverned, "ungoverned", repetition, order,
                                     full_budget, full_budget, pressure_runtimes, pressure_items);

            for (std::size_t runtime_count : {std::size_t{2}, std::size_t{4}, std::size_t{8}})
            {
                const auto scaling = run_concurrent(true, runtime_count,
                    concurrency_budget, concurrency_budget,
                    std::max<std::size_t>(32, concurrency_budget * 8), 120);
                write_concurrent_records(writer, scaling,
                    "governed_" + std::to_string(runtime_count) + "_runtimes",
                    repetition, "single", concurrency_budget, concurrency_budget,
                    runtime_count, std::max<std::size_t>(32, concurrency_budget * 8));
            }

            std::vector<std::size_t> budgets{1};
            if (effective >= 2) budgets.push_back(2);
            const std::size_t half = std::max<std::size_t>(1, effective / 2);
            if (std::find(budgets.begin(), budgets.end(), half) == budgets.end())
                budgets.push_back(half);
            if (std::find(budgets.begin(), budgets.end(), full_budget) == budgets.end())
                budgets.push_back(full_budget);
            for (std::size_t budget : budgets)
            {
                const auto scaled = run_concurrent(true, 2, budget, budget,
                    std::max<std::size_t>(32, budget * 16), 120);
                write_concurrent_records(writer, scaled,
                    "budget_" + std::to_string(budget), repetition, "single",
                    budget, budget, 2, std::max<std::size_t>(32, budget * 16));
            }
        }

        std::cout << "SmartParallel v1.8 raw benchmark evidence: "
                  << (output / "raw.csv") << '\n';
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SmartParallel v1.8 benchmark failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
