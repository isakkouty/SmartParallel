#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <smart/core/config.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/execution/runtime_capabilities.hpp>

namespace smart
{
struct NestedExecutionTraceRecord
{
    std::uint64_t root_loop_id = 0;
    std::uint64_t loop_id = 0;
    std::uint64_t parent_loop_id = 0;
    std::size_t callsite_hash = 0;
    std::size_t parent_callsite_hash = 0;
    std::size_t depth = 0;
    std::size_t iterations = 0;
    std::string phase = "decision";
    std::string requested_backend = "auto";
    std::string backend = "unconfirmed";
    bool backend_confirmed = false;
    std::size_t runtime_concurrency = 1;
    bool native_delegation = false;
    bool reused_runtime_domain = false;
    bool exceptional = false;
    std::string policy = "not_nested";
    std::string mechanism = "direct_execution";
    std::string decision_reason;
    bool cache_hit = false;
    bool profile_available = false;
    bool parallel = false;
    bool plan_snapshot_hit = false;
    double estimated_work_ms = 0.0;
    double measured_total_ms = 0.0;
    double nested_child_ms = 0.0;
    std::size_t nested_child_calls = 0;
    std::size_t requested_budget = 1;
    std::size_t effective_budget = 1;
    std::size_t leased_workers = 0;
    std::size_t max_root_leased_workers = 0;
    std::size_t chunk_size = 0;
    std::size_t total_chunks = 0;
    std::size_t helpers_submitted = 0;
    std::size_t helpers_started = 0;
    std::size_t helpers_useful = 0;
    std::size_t helpers_cancelled = 0;
    double helper_retire_tail_ms = 0.0;
    double helper_completion_signal_to_wake_ms = 0.0;
    std::size_t helper_wait_count = 0;
    double helper_in_flight_work_drain_ms = 0.0;
    double helper_actual_blocking_wait_ms = 0.0;
    double helper_completion_epilogue_ms = 0.0;
};

namespace detail
{
inline std::mutex& nested_trace_mutex()
{
    static std::mutex mutex;
    return mutex;
}

inline std::deque<NestedExecutionTraceRecord>& nested_trace_records()
{
    static std::deque<NestedExecutionTraceRecord> records;
    return records;
}

inline void append_nested_trace_record(NestedExecutionTraceRecord record)
{
    std::lock_guard<std::mutex> lock(nested_trace_mutex());
    const std::size_t maximum = runtime_limits::bounded_limit(
        effective_config().nested_execution_trace_max_records,
        runtime_limits::nested_trace_records);
    while (nested_trace_records().size() >= maximum)
        nested_trace_records().pop_front();
    nested_trace_records().push_back(std::move(record));
}
} // namespace detail

inline void clear_nested_execution_trace()
{
    std::lock_guard<std::mutex> lock(detail::nested_trace_mutex());
    detail::nested_trace_records().clear();
}

inline std::vector<NestedExecutionTraceRecord> nested_execution_trace_snapshot()
{
    std::lock_guard<std::mutex> lock(detail::nested_trace_mutex());
    const auto& records = detail::nested_trace_records();
    return std::vector<NestedExecutionTraceRecord>(records.begin(), records.end());
}

inline void write_nested_execution_trace_csv(
    const std::string& path,
    const std::vector<NestedExecutionTraceRecord>& records = nested_execution_trace_snapshot())
{
    std::ofstream out(path);
    if (!out)
        return;
    out << "root_loop_id,loop_id,parent_loop_id,callsite_hash,parent_callsite_hash,depth,iterations,"
           "phase,requested_backend,backend,backend_confirmed,runtime_concurrency,native_delegation,"
           "reused_runtime_domain,exceptional,policy,mechanism,decision_reason,cache_hit,profile_available,parallel,"
           "plan_snapshot_hit,estimated_work_ms,measured_total_ms,nested_child_ms,nested_child_calls,"
           "requested_budget,effective_budget,leased_workers,max_root_leased_workers,chunk_size,"
           "total_chunks,helpers_submitted,helpers_started,helpers_useful,helpers_cancelled,"
           "helper_retire_tail_ms,helper_completion_signal_to_wake_ms,helper_wait_count,"
           "helper_in_flight_work_drain_ms,helper_actual_blocking_wait_ms,"
           "helper_completion_epilogue_ms\n";
    out << std::fixed << std::setprecision(6);
    for (const auto& r : records)
    {
        auto safe = [](std::string value)
        {
            for (char& c : value)
                if (c == ',') c = ';';
            return value;
        };
        out << r.root_loop_id << ',' << r.loop_id << ',' << r.parent_loop_id << ','
            << r.callsite_hash << ',' << r.parent_callsite_hash << ',' << r.depth << ','
            << r.iterations << ',' << safe(r.phase) << ',' << safe(r.requested_backend) << ','
            << safe(r.backend) << ',' << (r.backend_confirmed ? 1 : 0) << ','
            << r.runtime_concurrency << ',' << (r.native_delegation ? 1 : 0) << ','
            << (r.reused_runtime_domain ? 1 : 0) << ',' << (r.exceptional ? 1 : 0) << ','
            << safe(r.policy) << ',' << safe(r.mechanism) << ',' << safe(r.decision_reason) << ','
            << (r.cache_hit ? 1 : 0) << ',' << (r.profile_available ? 1 : 0) << ','
            << (r.parallel ? 1 : 0) << ',' << (r.plan_snapshot_hit ? 1 : 0) << ','
            << r.estimated_work_ms << ',' << r.measured_total_ms << ',' << r.nested_child_ms << ','
            << r.nested_child_calls << ',' << r.requested_budget << ',' << r.effective_budget << ','
            << r.leased_workers << ',' << r.max_root_leased_workers << ',' << r.chunk_size << ','
            << r.total_chunks << ',' << r.helpers_submitted << ',' << r.helpers_started << ','
            << r.helpers_useful << ',' << r.helpers_cancelled << ',' << r.helper_retire_tail_ms << ','
            << r.helper_completion_signal_to_wake_ms << ',' << r.helper_wait_count << ','
            << r.helper_in_flight_work_drain_ms << ',' << r.helper_actual_blocking_wait_ms << ','
            << r.helper_completion_epilogue_ms << '\n';
    }
}

struct NestedPlanSnapshotKey
{
    std::size_t function_hash = 0;
    std::size_t element_size = 0;
    std::size_t iteration_bucket = 0;
    std::size_t depth = 0;
    std::size_t parent_callsite_hash = 0;
    std::size_t concurrency_budget = 1;
    ExecutionEngineType engine = ExecutionEngineType::Auto;
    std::size_t policy_signature = 0;
    std::size_t exact_iterations = 0;

    bool operator==(const NestedPlanSnapshotKey& other) const noexcept
    {
        return function_hash == other.function_hash && element_size == other.element_size
               && iteration_bucket == other.iteration_bucket && depth == other.depth
               && parent_callsite_hash == other.parent_callsite_hash
               && concurrency_budget == other.concurrency_budget && engine == other.engine
               && policy_signature == other.policy_signature
               && exact_iterations == other.exact_iterations;
    }
};

struct NestedPlanSnapshotKeyHasher
{
    std::size_t operator()(const NestedPlanSnapshotKey& key) const noexcept
    {
        std::size_t hash = key.function_hash;
        const auto combine = [&hash](std::size_t value)
        {
            hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        };
        combine(key.element_size);
        combine(key.iteration_bucket);
        combine(key.depth);
        combine(key.parent_callsite_hash);
        combine(key.concurrency_budget);
        combine(static_cast<std::size_t>(key.engine));
        combine(key.policy_signature);
        combine(key.exact_iterations);
        return hash;
    }
};

class NestedExecutionSession : public std::enable_shared_from_this<NestedExecutionSession>
{
  public:
    class ParticipantScope
    {
      public:
        explicit ParticipantScope(const NestedExecutionSession* owner)
            : owner_(owner)
        {
            if (owner_ != nullptr)
                owner_->enter_participant_scope();
        }

        ParticipantScope(const ParticipantScope&) = delete;
        ParticipantScope& operator=(const ParticipantScope&) = delete;

        ParticipantScope(ParticipantScope&& other) noexcept : owner_(other.owner_)
        {
            other.owner_ = nullptr;
        }

        ~ParticipantScope()
        {
            if (owner_ != nullptr)
                owner_->leave_participant_scope();
        }

      private:
        const NestedExecutionSession* owner_ = nullptr;
    };

    class Lease
    {
      public:
        Lease() = default;
        Lease(NestedExecutionSession* owner,
              std::size_t reserved_workers,
              std::size_t participant_budget) noexcept
            : owner_(owner),
              reserved_workers_(reserved_workers),
              participant_budget_(std::max<std::size_t>(1, participant_budget))
        {
        }

        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        Lease(Lease&& other) noexcept
            : owner_(other.owner_),
              reserved_workers_(other.reserved_workers_),
              participant_budget_(other.participant_budget_)
        {
            other.owner_ = nullptr;
            other.reserved_workers_ = 0;
            other.participant_budget_ = 1;
        }

        Lease& operator=(Lease&& other) noexcept
        {
            if (this == &other)
                return *this;
            release();
            owner_ = other.owner_;
            reserved_workers_ = other.reserved_workers_;
            participant_budget_ = other.participant_budget_;
            other.owner_ = nullptr;
            other.reserved_workers_ = 0;
            other.participant_budget_ = 1;
            return *this;
        }

        ~Lease() { release(); }

        std::size_t participant_budget() const noexcept { return participant_budget_; }
        std::size_t reserved_workers() const noexcept { return reserved_workers_; }
        bool valid() const noexcept { return owner_ != nullptr && reserved_workers_ != 0; }

      private:
        friend class NestedExecutionSession;

        void release() noexcept
        {
            if (owner_ != nullptr && reserved_workers_ != 0)
                owner_->release(reserved_workers_);
            owner_ = nullptr;
            reserved_workers_ = 0;
            participant_budget_ = 1;
        }

        NestedExecutionSession* owner_ = nullptr;
        std::size_t reserved_workers_ = 0;
        std::size_t participant_budget_ = 1;
    };

    explicit NestedExecutionSession(std::size_t total_budget, std::uint64_t root_loop_id = 0)
        : total_budget_(std::max<std::size_t>(1, total_budget)),
          root_loop_id_(root_loop_id)
    {
    }

    std::size_t total_budget() const noexcept { return total_budget_; }
    std::uint64_t root_loop_id() const noexcept { return root_loop_id_; }

    std::size_t leased_workers() const noexcept
    {
        return leased_workers_.load(std::memory_order_acquire);
    }

    std::size_t available_workers() const noexcept
    {
        const std::size_t leased = leased_workers();
        return leased >= total_budget_ ? 0 : total_budget_ - leased;
    }

    std::size_t maximum_leased_workers() const noexcept
    {
        return maximum_leased_workers_.load(std::memory_order_acquire);
    }

    std::size_t lease_invariant_violations() const noexcept
    {
        return lease_invariant_violations_.load(std::memory_order_acquire);
    }

    bool current_thread_owns_participant() const noexcept
    {
        const auto& ownership = participant_ownership();
        const auto it = std::find_if(
            ownership.begin(), ownership.end(),
            [this](const auto& entry)
            {
                return entry.first == this;
            });
        return it != ownership.end() && it->second != 0;
    }

    Lease reserve(std::size_t requested_workers) noexcept
    {
        if (requested_workers == 0)
            return {};

        std::size_t current = leased_workers_.load(std::memory_order_acquire);
        std::size_t acquired = 0;
        while (true)
        {
            const std::size_t available = current >= total_budget_ ? 0 : total_budget_ - current;
            acquired = std::min(requested_workers, available);
            if (leased_workers_.compare_exchange_weak(
                    current, current + acquired, std::memory_order_acq_rel, std::memory_order_acquire))
                break;
        }

        note_maximum(current + acquired);
        return acquired == 0 ? Lease{} : Lease(this, acquired, acquired);
    }

    Lease acquire(std::size_t requested_participants) noexcept
    {
        requested_participants = std::max<std::size_t>(1, requested_participants);
        const bool caller_already_leased = current_thread_owns_participant();
        const std::size_t requested_new_workers = caller_already_leased
            ? requested_participants - 1
            : requested_participants;
        Lease reservation = reserve(requested_new_workers);
        const std::size_t acquired = reservation.reserved_workers();
        const std::size_t participants = caller_already_leased ? 1 + acquired : acquired;
        if (!caller_already_leased && participants == 0)
        {
            lease_invariant_violations_.fetch_add(1, std::memory_order_relaxed);
            assert(false && "NestedExecutionSession could not reserve the caller participant");
            return {};
        }
        reservation.participant_budget_ = participants;
        return reservation;
    }

    std::optional<ExecutionPlan> find_plan_snapshot(const NestedPlanSnapshotKey& key) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = plan_snapshots_.find(key);
        return it == plan_snapshots_.end() ? std::nullopt
                                           : std::optional<ExecutionPlan>(it->second);
    }

    void store_plan_snapshot(const NestedPlanSnapshotKey& key, const ExecutionPlan& plan)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t maximum = runtime_limits::bounded_limit(
            effective_config().nested_plan_snapshot_max_entries,
            runtime_limits::nested_plan_snapshots);
        if (plan_snapshots_.size() >= maximum
            && plan_snapshots_.find(key) == plan_snapshots_.end())
            return;
        plan_snapshots_.emplace(key, plan);
    }

    std::size_t plan_snapshot_count() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return plan_snapshots_.size();
    }

    std::size_t pending_trace_count() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pending_traces_.size();
    }

    void begin_trace(NestedExecutionTraceRecord record)
    {
        if (!effective_config().enable_nested_execution_trace)
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        pending_traces_[record.loop_id] = std::move(record);
    }

    void update_backend_trace(std::uint64_t loop_id,
                              ExecutionEngineType backend,
                              std::size_t leased_workers,
                              std::size_t effective_budget,
                              std::size_t runtime_concurrency,
                              bool native_delegation,
                              bool reused_runtime_domain)
    {
        if (!effective_config().enable_nested_execution_trace)
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pending_traces_.find(loop_id);
        if (it == pending_traces_.end())
            return;
        auto& trace = it->second;
        trace.backend = runtime_name(backend);
        trace.backend_confirmed = true;
        trace.leased_workers = std::max(trace.leased_workers, leased_workers);
        trace.effective_budget = effective_budget;
        trace.runtime_concurrency = runtime_concurrency;
        trace.native_delegation = native_delegation;
        trace.reused_runtime_domain = reused_runtime_domain;
        trace.max_root_leased_workers = maximum_leased_workers();
    }

    template <typename HelpingResult>
    void update_scheduler_trace(std::uint64_t loop_id,
                                std::size_t leased_workers,
                                std::size_t effective_budget,
                                std::size_t total_chunks,
                                const HelpingResult& result)
    {
        if (!effective_config().enable_nested_execution_trace)
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pending_traces_.find(loop_id);
        if (it == pending_traces_.end())
            return;
        auto& trace = it->second;
        trace.leased_workers = std::max(trace.leased_workers, leased_workers);
        trace.effective_budget = effective_budget;
        trace.total_chunks += total_chunks;
        trace.helpers_submitted += result.helper_jobs_submitted;
        trace.helpers_started += result.helper_jobs_started;
        trace.helpers_useful += result.helper_jobs_useful;
        trace.helpers_cancelled += result.helper_jobs_cancelled;
        trace.helper_retire_tail_ms += result.work_complete_to_helpers_retired_ms;
        trace.helper_completion_signal_to_wake_ms += result.completion_signal_to_waiter_wake_ms;
        trace.helper_wait_count += result.wait_count;
        trace.helper_in_flight_work_drain_ms += result.in_flight_work_drain_ms;
        trace.helper_actual_blocking_wait_ms += result.actual_blocking_wait_ms;
        trace.helper_completion_epilogue_ms += result.completion_epilogue_ms;
        trace.max_root_leased_workers = maximum_leased_workers();
    }

    void finish_trace(std::uint64_t loop_id,
                      double measured_total_ms,
                      std::size_t nested_child_calls,
                      double nested_child_ms)
    {
        if (!effective_config().enable_nested_execution_trace)
            return;
        NestedExecutionTraceRecord trace;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = pending_traces_.find(loop_id);
            if (it == pending_traces_.end())
                return;
            trace = std::move(it->second);
            pending_traces_.erase(it);
        }
        trace.measured_total_ms = measured_total_ms;
        trace.nested_child_calls = nested_child_calls;
        trace.nested_child_ms = nested_child_ms;
        trace.max_root_leased_workers = maximum_leased_workers();
        detail::append_nested_trace_record(std::move(trace));
    }

    void abort_trace(std::uint64_t loop_id,
                     double measured_total_ms,
                     std::size_t nested_child_calls,
                     double nested_child_ms,
                     const char* reason = "exception")
    {
        if (!effective_config().enable_nested_execution_trace)
            return;
        NestedExecutionTraceRecord trace;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = pending_traces_.find(loop_id);
            if (it == pending_traces_.end())
                return;
            trace = std::move(it->second);
            pending_traces_.erase(it);
        }
        trace.phase = "exception";
        trace.exceptional = true;
        trace.decision_reason += trace.decision_reason.empty() ? reason : std::string(";") + reason;
        trace.measured_total_ms = measured_total_ms;
        trace.nested_child_calls = nested_child_calls;
        trace.nested_child_ms = nested_child_ms;
        trace.max_root_leased_workers = maximum_leased_workers();
        detail::append_nested_trace_record(std::move(trace));
    }

  private:
    using ParticipantOwnership = std::vector<std::pair<const NestedExecutionSession*, std::size_t>>;

    static ParticipantOwnership& participant_ownership() noexcept
    {
        static thread_local ParticipantOwnership ownership;
        return ownership;
    }

    void enter_participant_scope() const
    {
        auto& ownership = participant_ownership();
        auto it = std::find_if(
            ownership.begin(), ownership.end(),
            [this](const auto& entry)
            {
                return entry.first == this;
            });
        if (it == ownership.end())
            ownership.emplace_back(this, 1);
        else
            ++it->second;
    }

    void leave_participant_scope() const noexcept
    {
        auto& ownership = participant_ownership();
        auto it = std::find_if(
            ownership.begin(), ownership.end(),
            [this](const auto& entry)
            {
                return entry.first == this;
            });
        if (it == ownership.end() || it->second == 0)
        {
            lease_invariant_violations_.fetch_add(1, std::memory_order_relaxed);
            assert(false && "NestedExecutionSession participant scope underflow");
            return;
        }
        if (--it->second == 0)
            ownership.erase(it);
    }

    void note_maximum(std::size_t now) noexcept
    {
        std::size_t maximum = maximum_leased_workers_.load(std::memory_order_relaxed);
        while (maximum < now
               && !maximum_leased_workers_.compare_exchange_weak(
                   maximum, now, std::memory_order_relaxed, std::memory_order_relaxed))
        {
        }
    }

    void release(std::size_t count) noexcept
    {
        std::size_t current = leased_workers_.load(std::memory_order_acquire);
        while (true)
        {
            if (current < count)
            {
                lease_invariant_violations_.fetch_add(1, std::memory_order_relaxed);
                assert(false && "NestedExecutionSession lease counter underflow");
                return;
            }
            if (leased_workers_.compare_exchange_weak(
                    current, current - count, std::memory_order_acq_rel, std::memory_order_acquire))
                return;
        }
    }

    std::size_t total_budget_ = 1;
    std::uint64_t root_loop_id_ = 0;
    std::atomic<std::size_t> leased_workers_{0};
    std::atomic<std::size_t> maximum_leased_workers_{0};
    mutable std::atomic<std::size_t> lease_invariant_violations_{0};
    mutable std::mutex mutex_;
    std::unordered_map<NestedPlanSnapshotKey, ExecutionPlan, NestedPlanSnapshotKeyHasher>
        plan_snapshots_;
    std::unordered_map<std::uint64_t, NestedExecutionTraceRecord> pending_traces_;
};
} // namespace smart
