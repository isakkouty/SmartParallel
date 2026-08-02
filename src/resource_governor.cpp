#include <smart/runtime/resource_governor.hpp>
#include <smart/runtime/profile.hpp>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#endif
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace smart
{
namespace detail
{
struct GovernorState;

struct CancellationState
{
    std::atomic<bool> requested{false};
    std::mutex mutex;
    std::vector<std::weak_ptr<GovernorState>> governors;
};

struct PendingRequest
{
    LeaseRequest request;
    std::uint64_t sequence = 0;
    std::chrono::steady_clock::time_point enqueued_at;
    std::size_t bypass_count = 0;
    bool reservation_started = false;
    bool granted = false;
    bool removed = false;
    bool cancelled = false;
    std::size_t granted_workers = 0;
};

struct GovernorState
{
    GovernorState(ResourceGovernorOptions value, EffectiveCpuCapacityReport capacity_value)
        : options(value), capacity(std::move(capacity_value)),
          cv(std::make_shared<std::condition_variable>()) {}

    ResourceGovernorOptions options;
    EffectiveCpuCapacityReport capacity;
    mutable std::mutex mutex;
    std::shared_ptr<std::condition_variable> cv;
    std::deque<std::shared_ptr<PendingRequest>> pending;
    std::size_t active_permits = 0;
    std::size_t maximum_active_permits = 0;
    std::size_t active_leases = 0;
    std::uint64_t next_sequence = 1;
    std::uint64_t next_lease_identity = 1;
    std::uint64_t total_grants = 0;
    std::uint64_t total_releases = 0;
    std::uint64_t total_waits = 0;
    std::uint64_t total_cancellations = 0;
    std::uint64_t total_deadlines = 0;
    std::uint64_t total_bypasses = 0;
    std::uint64_t total_oldest_reservations = 0;
    bool shutting_down = false;
};

struct LeaseControl
{
    std::shared_ptr<GovernorState> governor;
    std::shared_ptr<LeaseControl> parent;
    std::size_t requested = 0;
    std::size_t granted = 0;
    std::uint64_t identity = 0;
    NestedLeaseMode mode = NestedLeaseMode::NotNested;
    bool owns_permits = false;

    ~LeaseControl() noexcept
    {
        if (!owns_permits || !governor || granted == 0)
            return;
        try
        {
            std::lock_guard<std::mutex> lock(governor->mutex);
            if (governor->active_permits >= granted)
                governor->active_permits -= granted;
            else
                governor->active_permits = 0;
            if (governor->active_leases > 0)
                --governor->active_leases;
            ++governor->total_releases;
            governor->cv->notify_all();
        }
        catch (...)
        {
            // Destruction is the noexcept release boundary.
        }
    }
};

std::shared_ptr<LeaseControl> execution_lease_control(const ::smart::ExecutionLease& lease) noexcept
{
    return lease.control_;
}
std::size_t lease_control_granted_workers(const std::shared_ptr<LeaseControl>& control) noexcept
{
    return control ? control->granted : 0;
}
std::uint64_t lease_control_identity(const std::shared_ptr<LeaseControl>& control) noexcept
{
    return control ? control->identity : 0;
}

namespace
{
std::size_t available_locked(const GovernorState& state) noexcept
{
    return state.options.cpu_budget > state.active_permits
        ? state.options.cpu_budget - state.active_permits : 0;
}

std::size_t grant_for(const LeaseRequest& request, std::size_t available) noexcept
{
    if (request.exact_grant_required)
        return available >= request.requested_workers ? request.requested_workers : 0;
    const std::size_t candidate = std::min(request.requested_workers, available);
    return candidate >= request.minimum_workers ? candidate : 0;
}

void erase_removed(GovernorState& state)
{
    state.pending.erase(
        std::remove_if(state.pending.begin(), state.pending.end(),
                       [](const auto& request) { return request->removed; }),
        state.pending.end());
}

void cancel_pending_locked(GovernorState& state,
                           const std::shared_ptr<PendingRequest>& pending)
{
    if (pending->removed || pending->granted || pending->cancelled)
        return;
    pending->cancelled = true;
    pending->removed = true;
    ++state.total_cancellations;
}

void remove_cancelled_locked(GovernorState& state)
{
    for (const auto& pending : state.pending)
    {
        if (!pending->removed && pending->request.cancellation.cancellation_requested())
            cancel_pending_locked(state, pending);
    }
    erase_removed(state);
}

void grant_one_locked(GovernorState& state,
                      const std::shared_ptr<PendingRequest>& pending,
                      std::size_t workers)
{
    pending->granted = true;
    pending->granted_workers = workers;
    pending->removed = true;
    state.active_permits += workers;
    state.maximum_active_permits = std::max(state.maximum_active_permits,
                                             state.active_permits);
    ++state.active_leases;
    ++state.total_grants;
}

void grant_waiters_locked(GovernorState& state)
{
    remove_cancelled_locked(state);
    if (state.shutting_down || state.pending.empty())
        return;

    bool progress = true;
    while (progress && !state.pending.empty())
    {
        progress = false;
        remove_cancelled_locked(state);
        if (state.pending.empty())
            break;
        const std::size_t available = available_locked(state);
        if (available == 0)
            break;

        auto oldest = state.pending.front();
        const std::size_t oldest_grant = grant_for(oldest->request, available);
        if (oldest_grant != 0)
        {
            grant_one_locked(state, oldest, oldest_grant);
            erase_removed(state);
            progress = true;
            state.cv->notify_all();
            continue;
        }

        const auto age = std::chrono::steady_clock::now() - oldest->enqueued_at;
        const bool aged = age >= state.options.oldest_request_reservation_age;
        const bool bypass_limit_reached =
            oldest->bypass_count >= state.options.bounded_bypass_limit;
        if ((aged || bypass_limit_reached) && !oldest->reservation_started)
        {
            oldest->reservation_started = true;
            ++state.total_oldest_reservations;
        }
        if (oldest->reservation_started)
            break;

        for (std::size_t index = 1; index < state.pending.size(); ++index)
        {
            auto candidate = state.pending[index];
            if (candidate->request.cancellation.cancellation_requested())
            {
                cancel_pending_locked(state, candidate);
                continue;
            }
            const std::size_t candidate_grant = grant_for(candidate->request, available);
            if (candidate_grant == 0)
                continue;
            grant_one_locked(state, candidate, candidate_grant);
            ++oldest->bypass_count;
            ++state.total_bypasses;
            erase_removed(state);
            progress = true;
            state.cv->notify_all();
            break;
        }
    }
}

#if defined(_WIN32)
std::size_t popcount_mask(std::uintptr_t mask) noexcept
{
    std::size_t count = 0;
    while (mask != 0)
    {
        count += static_cast<std::size_t>(mask & 1U);
        mask >>= 1U;
    }
    return count;
}
#endif

#if defined(__linux__)
std::size_t linux_affinity_capacity() noexcept
{
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0)
    {
        const int count = CPU_COUNT(&set);
        if (count > 0)
            return static_cast<std::size_t>(count);
    }
    return 0;
}

std::size_t quota_capacity(const std::string& quota_path,
                           const std::string& period_path) noexcept
{
    try
    {
        std::ifstream quota_file(quota_path);
        std::ifstream period_file(period_path);
        long long quota = -1;
        long long period = -1;
        if (!(quota_file >> quota) || !(period_file >> period)
            || quota <= 0 || period <= 0)
            return 0;
        return std::max<std::size_t>(1, static_cast<std::size_t>(
            (quota + period - 1) / period));
    }
    catch (...)
    {
        return 0;
    }
}

std::size_t linux_cgroup_capacity() noexcept
{
    try
    {
        std::ifstream unified("/sys/fs/cgroup/cpu.max");
        std::string quota_text;
        long long period = 0;
        if (unified >> quota_text >> period && quota_text != "max" && period > 0)
        {
            const long long quota = std::stoll(quota_text);
            if (quota > 0)
                return std::max<std::size_t>(1, static_cast<std::size_t>(
                    (quota + period - 1) / period));
        }
    }
    catch (...)
    {
    }
    return quota_capacity("/sys/fs/cgroup/cpu/cpu.cfs_quota_us",
                          "/sys/fs/cgroup/cpu/cpu.cfs_period_us");
}
#endif
} // namespace
} // namespace detail

bool CancellationToken::cancellation_requested() const noexcept
{
    return state_ && state_->requested.load(std::memory_order_acquire);
}
void CancellationToken::subscribe(
    const std::shared_ptr<detail::GovernorState>& governor) const noexcept
{
    if (!state_ || !governor)
        return;
    try
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->governors.erase(
            std::remove_if(state_->governors.begin(), state_->governors.end(),
                           [](const auto& value) { return value.expired(); }),
            state_->governors.end());
        state_->governors.push_back(governor);
        if (state_->requested.load(std::memory_order_acquire))
            governor->cv->notify_all();
    }
    catch (...)
    {
    }
}
CancellationSource::CancellationSource()
    : state_(std::make_shared<detail::CancellationState>()) {}
CancellationToken CancellationSource::token() const noexcept
{
    return CancellationToken(state_);
}
void CancellationSource::request_cancellation() noexcept
{
    if (!state_)
        return;
    state_->requested.store(true, std::memory_order_release);
    try
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        for (auto iterator = state_->governors.begin();
             iterator != state_->governors.end();)
        {
            if (auto governor = iterator->lock())
            {
                governor->cv->notify_all();
                ++iterator;
            }
            else
            {
                iterator = state_->governors.erase(iterator);
            }
        }
    }
    catch (...)
    {
    }
}

const char* lease_wait_policy_name(LeaseWaitPolicy value) noexcept
{
    switch (value)
    {
        case LeaseWaitPolicy::FailImmediately: return "fail_immediately";
        case LeaseWaitPolicy::Wait: return "wait";
        case LeaseWaitPolicy::WaitUntilDeadline: return "wait_until_deadline";
    }
    return "unknown";
}
const char* lease_acquire_status_name(LeaseAcquireStatus value) noexcept
{
    switch (value)
    {
        case LeaseAcquireStatus::Granted: return "granted";
        case LeaseAcquireStatus::ImpossibleRequest: return "impossible_request";
        case LeaseAcquireStatus::InvalidRequest: return "invalid_request";
        case LeaseAcquireStatus::WouldBlock: return "would_block";
        case LeaseAcquireStatus::DeadlineExpired: return "deadline_expired";
        case LeaseAcquireStatus::Cancelled: return "cancelled";
        case LeaseAcquireStatus::GovernorShuttingDown: return "governor_shutting_down";
    }
    return "unknown";
}
const char* nested_lease_mode_name(NestedLeaseMode value) noexcept
{
    switch (value)
    {
        case NestedLeaseMode::NotNested: return "not_nested";
        case NestedLeaseMode::ReuseParent: return "reuse_parent";
        case NestedLeaseMode::PartitionParent: return "partition_parent";
        case NestedLeaseMode::SequentialWithinParent: return "sequential_within_parent";
    }
    return "unknown";
}
const char* control_scope_name(ControlScope value) noexcept
{
    switch (value)
    {
        case ControlScope::PerCall: return "per_call";
        case ControlScope::PerThread: return "per_thread";
        case ControlScope::PerTask: return "per_task";
        case ControlScope::ProcessGlobal: return "process_global";
    }
    return "unknown";
}
const char* control_strength_name(ControlStrength value) noexcept
{
    switch (value)
    {
        case ControlStrength::Exact: return "exact";
        case ControlStrength::UpperBound: return "upper_bound";
        case ControlStrength::Advisory: return "advisory";
        case ControlStrength::SerializedProcessGlobal: return "serialized_process_global";
        case ControlStrength::Unsupported: return "unsupported";
    }
    return "unknown";
}

std::size_t ExecutionLease::requested_workers() const noexcept
{
    return control_ ? control_->requested : 0;
}
std::size_t ExecutionLease::granted_workers() const noexcept
{
    return control_ ? control_->granted : 0;
}
std::uint64_t ExecutionLease::identity() const noexcept
{
    return control_ ? control_->identity : 0;
}
bool ExecutionLease::inherited() const noexcept
{
    return control_ && control_->mode != NestedLeaseMode::NotNested;
}
bool ExecutionLease::partitioned() const noexcept
{
    return control_ && control_->mode == NestedLeaseMode::PartitionParent;
}
NestedLeaseMode ExecutionLease::nested_mode() const noexcept
{
    return control_ ? control_->mode : NestedLeaseMode::NotNested;
}
std::string ExecutionLease::fingerprint() const
{
    if (!control_)
        return {};
    std::ostringstream identity;
    identity << "governor=";
    if (control_->governor)
        identity << control_->governor->options.cpu_budget;
    else
        identity << 0;
    identity << ";requested=" << control_->requested
             << ";granted=" << control_->granted
             << ";nested=" << nested_lease_mode_name(control_->mode);
    return sha256_hex(identity.str());
}
ExecutionLease ExecutionLease::inherit(std::size_t worker_limit) const
{
    if (!control_)
        return {};
    auto child = std::make_shared<detail::LeaseControl>();
    child->governor = control_->governor;
    child->parent = control_;
    child->requested = worker_limit == 0 ? control_->granted : worker_limit;
    child->granted = worker_limit == 0
        ? control_->granted : std::min(worker_limit, control_->granted);
    child->identity = control_->identity;
    child->mode = child->granted <= 1
        ? NestedLeaseMode::SequentialWithinParent : NestedLeaseMode::ReuseParent;
    child->owns_permits = false;
    return ExecutionLease(std::move(child));
}

EffectiveCpuCapacityReport effective_cpu_capacity() noexcept
{
    EffectiveCpuCapacityReport result;
    const auto hardware = std::thread::hardware_concurrency();
    result.capacity = hardware == 0 ? 1 : static_cast<std::size_t>(hardware);
    result.source = hardware == 0 ? "fallback_one" : "hardware_concurrency";

#if defined(__linux__)
    const std::size_t affinity = detail::linux_affinity_capacity();
    const std::size_t cgroup = detail::linux_cgroup_capacity();
    if (affinity > 0)
    {
        result.capacity = affinity;
        result.source = "linux_sched_affinity";
    }
    if (cgroup > 0 && cgroup < result.capacity)
    {
        result.capacity = cgroup;
        result.source += "+cgroup_cpu_quota";
    }
#elif defined(_WIN32)
    const WORD groups = GetActiveProcessorGroupCount();
    if (groups <= 1)
    {
        DWORD_PTR process_mask = 0;
        DWORD_PTR system_mask = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask)
            && process_mask != 0)
        {
            result.capacity = std::max<std::size_t>(1,
                detail::popcount_mask(static_cast<std::uintptr_t>(process_mask)));
            result.source = "windows_process_affinity";
        }
    }
    else
    {
        PROCESSOR_NUMBER number{};
        GetCurrentProcessorNumberEx(&number);
        const DWORD count = GetActiveProcessorCount(number.Group);
        result.capacity = count == 0 ? 1 : static_cast<std::size_t>(count);
        result.reliable = false;
        result.source = "windows_current_processor_group";
        result.diagnostic =
            "multiple Windows processor groups are active; v1.8 conservatively "
            "uses the current group capacity and does not claim complete multi-group governance";
    }
#endif
    return result;
}

std::size_t effective_cpu_availability() noexcept
{
    return effective_cpu_capacity().capacity;
}

ResourceGovernor::ResourceGovernor(ResourceGovernorOptions options)
{
    const EffectiveCpuCapacityReport capacity = effective_cpu_capacity();
    if (options.cpu_budget == 0)
        throw std::invalid_argument(
            "SmartParallel ResourceGovernor requires a positive CPU budget");
    if (options.cpu_budget > capacity.capacity)
        throw std::invalid_argument(
            "SmartParallel ResourceGovernor CPU budget exceeds effective process CPU availability");
    if (options.bounded_bypass_limit == 0)
        options.bounded_bypass_limit = 1;
    if (options.oldest_request_reservation_age.count() < 0)
        throw std::invalid_argument(
            "SmartParallel oldest-request reservation age must not be negative");
    state_ = std::make_shared<detail::GovernorState>(options, capacity);
}
ResourceGovernor::~ResourceGovernor()
{
    request_shutdown();
}
std::size_t ResourceGovernor::cpu_budget() const noexcept
{
    return state_ ? state_->options.cpu_budget : 0;
}
std::string ResourceGovernor::fingerprint() const
{
    if (!state_)
        return {};
    std::ostringstream identity;
    identity << "resource_governor_v2;cpu_budget=" << state_->options.cpu_budget
             << ";bounded_bypass_limit=" << state_->options.bounded_bypass_limit
             << ";reservation_age_ms="
             << state_->options.oldest_request_reservation_age.count();
    return sha256_hex(identity.str());
}
ResourceSnapshot ResourceGovernor::snapshot() const
{
    ResourceSnapshot result;
    if (!state_)
        return result;
    std::lock_guard<std::mutex> lock(state_->mutex);
    result.cpu_budget = state_->options.cpu_budget;
    result.active_permits = state_->active_permits;
    result.maximum_active_permits = state_->maximum_active_permits;
    result.available_permits = detail::available_locked(*state_);
    result.pending_requests = static_cast<std::size_t>(std::count_if(
        state_->pending.begin(), state_->pending.end(),
        [](const auto& request) { return !request->removed; }));
    result.active_leases = state_->active_leases;
    result.total_grants = state_->total_grants;
    result.total_releases = state_->total_releases;
    result.total_waits = state_->total_waits;
    result.total_cancellations = state_->total_cancellations;
    result.total_deadlines = state_->total_deadlines;
    result.total_bypasses = state_->total_bypasses;
    result.total_oldest_reservations = state_->total_oldest_reservations;
    result.shutting_down = state_->shutting_down;
    result.effective_cpu_capacity = state_->capacity.capacity;
    result.effective_capacity_reliable = state_->capacity.reliable;
    result.effective_capacity_source = state_->capacity.source;
    result.effective_capacity_diagnostic = state_->capacity.diagnostic;
    if (!state_->pending.empty())
    {
        const auto now = std::chrono::steady_clock::now();
        for (const auto& request : state_->pending)
        {
            if (request->removed)
                continue;
            result.oldest_waiter_age_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now - request->enqueued_at).count());
            break;
        }
    }
    return result;
}
LeaseAcquireResult ResourceGovernor::acquire(const LeaseRequest& source_request)
{
    LeaseAcquireResult result;
    const auto started = std::chrono::steady_clock::now();
    if (!state_)
    {
        result.status = LeaseAcquireStatus::GovernorShuttingDown;
        result.reason = "governor state is unavailable";
        return result;
    }

    LeaseRequest request = source_request;
    if (request.preferred_workers == 0)
        request.preferred_workers = request.requested_workers;
    if (request.maximum_workers == 0)
        request.maximum_workers = std::max(request.requested_workers,
                                           request.preferred_workers);
    if (request.requested_workers == 0)
        request.requested_workers = request.preferred_workers;

    result.requested_workers = request.requested_workers;
    result.minimum_workers = request.minimum_workers;
    result.preferred_workers = request.preferred_workers;
    result.maximum_workers = request.maximum_workers;

    const bool invalid_range = request.minimum_workers == 0
        || request.requested_workers == 0
        || request.preferred_workers == 0
        || request.maximum_workers == 0
        || request.minimum_workers > request.requested_workers
        || request.minimum_workers > request.preferred_workers
        || request.requested_workers > request.maximum_workers
        || request.preferred_workers > request.maximum_workers;
    const bool invalid_exact = request.exact_grant_required
        && (request.minimum_workers != request.requested_workers
            || request.preferred_workers != request.requested_workers
            || request.maximum_workers != request.requested_workers);
    if (invalid_range || invalid_exact)
    {
        result.status = LeaseAcquireStatus::InvalidRequest;
        result.reason = request.exact_grant_required
            ? "exact request requires minimum, preferred, requested and maximum workers to match"
            : "worker request must satisfy 1 <= minimum <= requested/preferred <= maximum";
        return result;
    }
    if (request.minimum_workers > state_->options.cpu_budget
        || (request.exact_grant_required
            && request.requested_workers > state_->options.cpu_budget))
    {
        result.status = LeaseAcquireStatus::ImpossibleRequest;
        result.reason = "request exceeds the permanent governor budget";
        return result;
    }
    if (request.wait_policy == LeaseWaitPolicy::WaitUntilDeadline
        && request.deadline == std::chrono::steady_clock::time_point{})
    {
        result.status = LeaseAcquireStatus::InvalidRequest;
        result.reason = "deadline wait policy requires a deadline";
        return result;
    }
    if (request.cancellation.cancellation_requested())
    {
        result.status = LeaseAcquireStatus::Cancelled;
        result.reason = "request was cancelled before admission";
        return result;
    }

    request.cancellation.subscribe(state_);
    auto pending = std::make_shared<detail::PendingRequest>();
    pending->request = request;
    pending->enqueued_at = started;

    std::unique_lock<std::mutex> lock(state_->mutex);
    if (state_->shutting_down)
    {
        result.status = LeaseAcquireStatus::GovernorShuttingDown;
        result.reason = "governor is shutting down";
        return result;
    }
    pending->sequence = state_->next_sequence++;
    state_->pending.push_back(pending);
    detail::grant_waiters_locked(*state_);

    if (pending->cancelled)
    {
        result.status = LeaseAcquireStatus::Cancelled;
        result.reason = "request was cancelled during admission";
        return result;
    }
    if (!pending->granted && request.wait_policy == LeaseWaitPolicy::FailImmediately)
    {
        pending->removed = true;
        detail::erase_removed(*state_);
        result.status = LeaseAcquireStatus::WouldBlock;
        result.reason = "requested capacity is not immediately available";
        return result;
    }

    if (!pending->granted)
        ++state_->total_waits;

    while (!pending->granted)
    {
        if (state_->shutting_down)
        {
            pending->removed = true;
            detail::erase_removed(*state_);
            result.status = LeaseAcquireStatus::GovernorShuttingDown;
            result.reason = "governor shut down while request was waiting";
            break;
        }
        if (request.cancellation.cancellation_requested() || pending->cancelled)
        {
            detail::cancel_pending_locked(*state_, pending);
            detail::erase_removed(*state_);
            result.status = LeaseAcquireStatus::Cancelled;
            result.reason = "request was cancelled while waiting";
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        if (request.wait_policy == LeaseWaitPolicy::WaitUntilDeadline
            && now >= request.deadline)
        {
            pending->removed = true;
            ++state_->total_deadlines;
            detail::erase_removed(*state_);
            result.status = LeaseAcquireStatus::DeadlineExpired;
            result.reason = "lease deadline expired";
            break;
        }

        if (request.wait_policy == LeaseWaitPolicy::WaitUntilDeadline)
            state_->cv->wait_until(lock, request.deadline);
        else
            state_->cv->wait(lock);
        detail::grant_waiters_locked(*state_);
    }

    result.wait_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started);
    result.bypass_count = pending->bypass_count;
    if (!pending->granted)
        return result;

    auto control = std::make_shared<detail::LeaseControl>();
    control->governor = state_;
    control->requested = request.requested_workers;
    control->granted = pending->granted_workers;
    control->identity = state_->next_lease_identity++;
    control->mode = NestedLeaseMode::NotNested;
    control->owns_permits = true;
    result.status = LeaseAcquireStatus::Granted;
    result.granted_workers = control->granted;
    result.reason = control->granted == request.requested_workers
        ? "requested grant admitted" : "flexible request admitted with reduced grant";
    result.lease = ExecutionLease(std::move(control));
    return result;
}
void ResourceGovernor::request_shutdown() noexcept
{
    if (!state_)
        return;
    try
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->shutting_down = true;
        state_->cv->notify_all();
    }
    catch (...)
    {
    }
}

std::string resource_decision_fingerprint(const ResourceDecisionReport& report)
{
    std::ostringstream identity;
    identity << "resource_decision_v2"
             << ";governor=" << report.governor_fingerprint
             << ";budget=" << report.process_cpu_budget
             << ";runtime_ceiling=" << report.runtime_worker_ceiling
             << ";requested=" << report.requested_workers
             << ";minimum=" << report.minimum_workers
             << ";preferred=" << report.preferred_workers
             << ";maximum=" << report.maximum_workers
             << ";granted=" << report.granted_workers
             << ";cap=" << report.scheduler_concurrency_cap
             << ";exact=" << (report.exact_grant_required ? 1 : 0)
             << ";wait_policy=" << lease_wait_policy_name(report.wait_policy)
             << ";nested=" << nested_lease_mode_name(report.nested_mode)
             << ";scope=" << control_scope_name(report.provider_control_scope)
             << ";strength=" << control_strength_name(report.provider_control_strength)
             << ";serialized=" << (report.provider_serialized ? 1 : 0)
             << ";deterministic=" << (report.deterministic_requirement ? 1 : 0)
             << ";scheduler=" << report.scheduler
             << ";provider=" << report.provider;
    return sha256_hex(identity.str());
}

std::shared_ptr<ResourceGovernor> default_resource_governor()
{
    static std::shared_ptr<ResourceGovernor> governor = []
    {
        ResourceGovernorOptions options;
        options.cpu_budget = effective_cpu_availability();
        return std::make_shared<ResourceGovernor>(options);
    }();
    return governor;
}
} // namespace smart
