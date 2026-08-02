#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace smart
{
namespace detail
{
struct GovernorState;
struct LeaseControl;
struct CancellationState;
}

enum class LeaseWaitPolicy
{
    FailImmediately,
    Wait,
    WaitUntilDeadline
};

enum class LeaseAcquireStatus
{
    Granted,
    ImpossibleRequest,
    InvalidRequest,
    WouldBlock,
    DeadlineExpired,
    Cancelled,
    GovernorShuttingDown
};

enum class NestedLeaseMode
{
    NotNested,
    ReuseParent,
    PartitionParent,
    SequentialWithinParent
};

enum class ControlScope
{
    PerCall,
    PerThread,
    PerTask,
    ProcessGlobal
};

enum class ControlStrength
{
    Exact,
    UpperBound,
    Advisory,
    SerializedProcessGlobal,
    Unsupported
};

struct ProviderControlCapabilities
{
    ControlScope scope = ControlScope::PerCall;
    ControlStrength strength = ControlStrength::Exact;
    bool concurrently_reconfigurable = true;
    bool safely_restorable = true;
    bool actual_participation_observable = true;
    bool requires_serialized_invocation = false;
};

class CancellationToken
{
  public:
    CancellationToken() noexcept = default;
    bool cancellation_requested() const noexcept;
    explicit operator bool() const noexcept { return static_cast<bool>(state_); }

  private:
    explicit CancellationToken(std::shared_ptr<detail::CancellationState> state) noexcept
        : state_(std::move(state)) {}
    void subscribe(const std::shared_ptr<detail::GovernorState>& governor) const noexcept;
    std::shared_ptr<detail::CancellationState> state_;
    friend class CancellationSource;
    friend class ResourceGovernor;
};

class CancellationSource
{
  public:
    CancellationSource();
    CancellationToken token() const noexcept;
    void request_cancellation() noexcept;

  private:
    std::shared_ptr<detail::CancellationState> state_;
};

struct ResourceGovernorOptions
{
    std::size_t cpu_budget = 0;
    std::size_t bounded_bypass_limit = 8;
    std::chrono::milliseconds oldest_request_reservation_age{50};
};

struct LeaseRequest
{
    // requested_workers is the immediate admission target. For flexible
    // requests it normally equals preferred_workers; maximum_workers remains
    // a hard Runtime/operation ceiling.
    std::size_t requested_workers = 1;
    std::size_t minimum_workers = 1;
    std::size_t preferred_workers = 0;
    std::size_t maximum_workers = 0;
    bool exact_grant_required = false;
    LeaseWaitPolicy wait_policy = LeaseWaitPolicy::FailImmediately;
    std::chrono::steady_clock::time_point deadline{};
    CancellationToken cancellation;
    std::string operation_identity;
    std::string runtime_fingerprint;
};

struct ResourceSnapshot
{
    std::size_t cpu_budget = 0;
    std::size_t active_permits = 0;
    std::size_t maximum_active_permits = 0;
    std::size_t available_permits = 0;
    std::size_t pending_requests = 0;
    std::size_t active_leases = 0;
    std::uint64_t total_grants = 0;
    std::uint64_t total_releases = 0;
    std::uint64_t total_waits = 0;
    std::uint64_t total_cancellations = 0;
    std::uint64_t total_deadlines = 0;
    std::uint64_t total_bypasses = 0;
    std::uint64_t total_oldest_reservations = 0;
    std::uint64_t oldest_waiter_age_ns = 0;
    std::size_t effective_cpu_capacity = 0;
    bool effective_capacity_reliable = true;
    std::string effective_capacity_source;
    std::string effective_capacity_diagnostic;
    bool shutting_down = false;
};

class ExecutionLease;

namespace detail
{
struct GovernorState;
struct LeaseControl;
struct CancellationState;
std::shared_ptr<LeaseControl> execution_lease_control(const ::smart::ExecutionLease&) noexcept;
std::size_t lease_control_granted_workers(const std::shared_ptr<LeaseControl>&) noexcept;
std::uint64_t lease_control_identity(const std::shared_ptr<LeaseControl>&) noexcept;
}

class ExecutionLease
{
  public:
    ExecutionLease() noexcept = default;
    ExecutionLease(ExecutionLease&&) noexcept = default;
    ExecutionLease& operator=(ExecutionLease&&) noexcept = default;
    ExecutionLease(const ExecutionLease&) = delete;
    ExecutionLease& operator=(const ExecutionLease&) = delete;
    ~ExecutionLease() noexcept = default;

    explicit operator bool() const noexcept { return static_cast<bool>(control_); }
    std::size_t requested_workers() const noexcept;
    std::size_t granted_workers() const noexcept;
    std::uint64_t identity() const noexcept;
    bool inherited() const noexcept;
    bool partitioned() const noexcept;
    NestedLeaseMode nested_mode() const noexcept;
    std::string fingerprint() const;

    ExecutionLease inherit(std::size_t worker_limit = 0) const;

  private:
    explicit ExecutionLease(std::shared_ptr<detail::LeaseControl> control) noexcept
        : control_(std::move(control)) {}
    std::shared_ptr<detail::LeaseControl> control_;
    friend class ResourceGovernor;
    friend std::shared_ptr<detail::LeaseControl>
        detail::execution_lease_control(const ExecutionLease&) noexcept;
};

struct LeaseAcquireResult
{
    LeaseAcquireStatus status = LeaseAcquireStatus::InvalidRequest;
    ExecutionLease lease;
    std::size_t requested_workers = 0;
    std::size_t minimum_workers = 0;
    std::size_t preferred_workers = 0;
    std::size_t maximum_workers = 0;
    std::size_t granted_workers = 0;
    std::chrono::nanoseconds wait_duration{0};
    std::size_t bypass_count = 0;
    std::string reason;

    explicit operator bool() const noexcept
    {
        return status == LeaseAcquireStatus::Granted && static_cast<bool>(lease);
    }
};

struct ResourceDecisionReport
{
    std::string operation_identity;
    std::string runtime_fingerprint;
    std::string governor_fingerprint;
    std::size_t process_cpu_budget = 0;
    std::size_t runtime_worker_ceiling = 0;
    std::size_t requested_workers = 0;
    std::size_t minimum_workers = 0;
    std::size_t preferred_workers = 0;
    std::size_t maximum_workers = 0;
    std::size_t granted_workers = 0;
    std::size_t scheduler_concurrency_cap = 0;
    std::size_t observed_participating_threads = 0;
    bool exact_grant_required = false;
    LeaseWaitPolicy wait_policy = LeaseWaitPolicy::FailImmediately;
    LeaseAcquireStatus admission_status = LeaseAcquireStatus::InvalidRequest;
    std::chrono::nanoseconds wait_duration{0};
    std::size_t nesting_depth = 0;
    NestedLeaseMode nested_mode = NestedLeaseMode::NotNested;
    std::uint64_t lease_identity = 0;
    std::uint64_t parent_lease_identity = 0;
    ControlScope provider_control_scope = ControlScope::PerCall;
    ControlStrength provider_control_strength = ControlStrength::Exact;
    bool provider_serialized = false;
    bool deterministic_requirement = false;
    std::string scheduler;
    std::string provider;
    std::string rejection_or_restriction_reason;
    std::string stable_fingerprint;
};

struct EffectiveCpuCapacityReport
{
    std::size_t capacity = 1;
    bool reliable = true;
    std::string source;
    std::string diagnostic;
};

class ResourceGovernor
{
  public:
    explicit ResourceGovernor(ResourceGovernorOptions options);
    ~ResourceGovernor();
    ResourceGovernor(const ResourceGovernor&) = delete;
    ResourceGovernor& operator=(const ResourceGovernor&) = delete;

    std::size_t cpu_budget() const noexcept;
    std::string fingerprint() const;
    ResourceSnapshot snapshot() const;
    LeaseAcquireResult acquire(const LeaseRequest& request);
    void request_shutdown() noexcept;

  private:
    std::shared_ptr<detail::GovernorState> state_;
};

std::shared_ptr<ResourceGovernor> default_resource_governor();
EffectiveCpuCapacityReport effective_cpu_capacity() noexcept;
std::size_t effective_cpu_availability() noexcept;

const char* lease_wait_policy_name(LeaseWaitPolicy) noexcept;
const char* lease_acquire_status_name(LeaseAcquireStatus) noexcept;
const char* nested_lease_mode_name(NestedLeaseMode) noexcept;
const char* control_scope_name(ControlScope) noexcept;
const char* control_strength_name(ControlStrength) noexcept;
std::string resource_decision_fingerprint(const ResourceDecisionReport&);
}
