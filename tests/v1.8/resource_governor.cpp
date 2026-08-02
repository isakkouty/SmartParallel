#include <smart/runtime/resource_governor.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
using namespace std::chrono_literals;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

smart::LeaseRequest exact_request(
    std::size_t workers,
    smart::LeaseWaitPolicy policy = smart::LeaseWaitPolicy::FailImmediately)
{
    smart::LeaseRequest value;
    value.requested_workers = workers;
    value.minimum_workers = workers;
    value.preferred_workers = workers;
    value.maximum_workers = workers;
    value.exact_grant_required = true;
    value.wait_policy = policy;
    return value;
}

smart::LeaseRequest flexible_request(
    std::size_t minimum,
    std::size_t preferred,
    std::size_t maximum,
    smart::LeaseWaitPolicy policy = smart::LeaseWaitPolicy::FailImmediately)
{
    smart::LeaseRequest value;
    value.requested_workers = preferred;
    value.minimum_workers = minimum;
    value.preferred_workers = preferred;
    value.maximum_workers = maximum;
    value.exact_grant_required = false;
    value.wait_policy = policy;
    return value;
}

void basic_accounting_and_inheritance()
{
    smart::ResourceGovernor governor({4, 2, 50ms});
    require(governor.cpu_budget() == 4, "governor budget mismatch");
    {
        auto acquired = governor.acquire(exact_request(3));
        require(static_cast<bool>(acquired), "basic lease acquisition failed");
        require(acquired.lease.granted_workers() == 3, "grant mismatch");
        require(governor.snapshot().active_permits == 3,
                "active permit count mismatch");
        smart::ExecutionLease moved = std::move(acquired.lease);
        require(moved && !acquired.lease, "lease move ownership failed");
        auto inherited = moved.inherit(2);
        require(inherited.inherited() && inherited.granted_workers() == 2,
                "inherited lease mismatch");
        require(inherited.nested_mode() == smart::NestedLeaseMode::ReuseParent,
                "inherited lease mode mismatch");
        require(governor.snapshot().active_permits == 3,
                "inherited lease acquired independent permits");
    }
    const auto snapshot = governor.snapshot();
    require(snapshot.active_permits == 0 && snapshot.active_leases == 0,
            "lease did not release permits");
    require(snapshot.total_grants == 1 && snapshot.total_releases == 1,
            "grant/release telemetry mismatch");
}

void request_validation_and_outcomes()
{
    bool zero_rejected = false;
    try
    {
        smart::ResourceGovernor invalid({0, 2, 50ms});
    }
    catch (const std::invalid_argument&)
    {
        zero_rejected = true;
    }
    require(zero_rejected, "zero CPU budget was not rejected");

    smart::ResourceGovernor governor({2, 2, 50ms});
    auto invalid = flexible_request(2, 1, 2);
    require(governor.acquire(invalid).status == smart::LeaseAcquireStatus::InvalidRequest,
            "invalid flexible range was accepted");
    require(governor.acquire(exact_request(3)).status
                == smart::LeaseAcquireStatus::ImpossibleRequest,
            "impossible exact request did not fail");

    auto held = governor.acquire(exact_request(2));
    require(static_cast<bool>(held), "could not acquire full budget");
    require(governor.acquire(exact_request(1)).status
                == smart::LeaseAcquireStatus::WouldBlock,
            "fail-immediately request did not report would-block");

    auto deadline = exact_request(1, smart::LeaseWaitPolicy::WaitUntilDeadline);
    deadline.deadline = std::chrono::steady_clock::now() + 15ms;
    require(governor.acquire(deadline).status
                == smart::LeaseAcquireStatus::DeadlineExpired,
            "deadline request did not expire");
}

void flexible_partial_grant_and_exception_release()
{
    smart::ResourceGovernor governor({4, 4, 50ms});
    auto held = governor.acquire(exact_request(2));
    require(static_cast<bool>(held), "flexible-grant setup failed");

    auto reduced = governor.acquire(flexible_request(1, 3, 4));
    require(static_cast<bool>(reduced) && reduced.granted_workers == 2,
            "flexible request did not use the available partial grant");
    require(reduced.preferred_workers == 3 && reduced.maximum_workers == 4,
            "flexible request fields were not preserved");

    const auto releases_before = governor.snapshot().total_releases;
    try
    {
        auto scoped = std::move(reduced.lease);
        require(static_cast<bool>(scoped), "exception-release lease move failed");
        throw std::runtime_error("injected operation failure");
    }
    catch (const std::runtime_error&)
    {
    }
    require(governor.snapshot().total_releases == releases_before + 1,
            "exception unwinding did not release the lease exactly once");
}

void direct_cancellation_notification()
{
    smart::ResourceGovernor governor({1, 4, 50ms});
    auto held = governor.acquire(exact_request(1));
    smart::CancellationSource cancellation;
    auto pending = exact_request(1, smart::LeaseWaitPolicy::Wait);
    pending.cancellation = cancellation.token();
    std::atomic<smart::LeaseAcquireStatus> status{
        smart::LeaseAcquireStatus::InvalidRequest};
    const auto started = std::chrono::steady_clock::now();
    std::thread waiter([&]
    {
        status.store(governor.acquire(pending).status, std::memory_order_release);
    });
    while (governor.snapshot().pending_requests == 0)
        std::this_thread::yield();
    cancellation.request_cancellation();
    waiter.join();
    const auto latency = std::chrono::steady_clock::now() - started;
    require(status.load(std::memory_order_acquire)
                == smart::LeaseAcquireStatus::Cancelled,
            "waiting request did not cancel");
    require(latency < 100ms,
            "cancellation response still appears to depend on coarse polling");
}

void cancellation_deadline_shutdown_races()
{
    for (int iteration = 0; iteration < 100; ++iteration)
    {
        smart::ResourceGovernor governor({1, 4, 20ms});
        auto held = governor.acquire(exact_request(1));
        smart::CancellationSource cancellation;
        auto pending = exact_request(
            1, (iteration % 3) == 0
                ? smart::LeaseWaitPolicy::WaitUntilDeadline
                : smart::LeaseWaitPolicy::Wait);
        if (pending.wait_policy == smart::LeaseWaitPolicy::WaitUntilDeadline)
            pending.deadline = std::chrono::steady_clock::now() + 25ms;
        pending.cancellation = cancellation.token();
        std::atomic<smart::LeaseAcquireStatus> status{
            smart::LeaseAcquireStatus::InvalidRequest};
        std::thread waiter([&]
        {
            status.store(governor.acquire(pending).status,
                         std::memory_order_release);
        });
        while (governor.snapshot().pending_requests == 0)
            std::this_thread::yield();
        switch (iteration % 3)
        {
            case 0: cancellation.request_cancellation(); break;
            case 1: held.lease = {}; cancellation.request_cancellation(); break;
            default: governor.request_shutdown(); break;
        }
        waiter.join();
        const auto observed = status.load(std::memory_order_acquire);
        require(observed == smart::LeaseAcquireStatus::Granted
                    || observed == smart::LeaseAcquireStatus::Cancelled
                    || observed == smart::LeaseAcquireStatus::DeadlineExpired
                    || observed == smart::LeaseAcquireStatus::GovernorShuttingDown,
                "admission race produced an invalid outcome");
        held.lease = {};
        require(governor.snapshot().active_permits == 0,
                "admission race leaked permits");
    }
}

void waiting_and_shutdown()
{
    smart::ResourceGovernor governor({2, 2, 50ms});
    auto held = governor.acquire(exact_request(2));
    std::atomic<bool> acquired{false};
    std::thread waiter([&]
    {
        auto result = governor.acquire(
            exact_request(2, smart::LeaseWaitPolicy::Wait));
        acquired.store(static_cast<bool>(result));
    });
    std::this_thread::sleep_for(5ms);
    require(!acquired.load(), "waiting request bypassed unavailable capacity");
    held.lease = {};
    waiter.join();
    require(acquired.load(), "waiting request was not granted after release");

    auto held_again = governor.acquire(exact_request(2));
    std::atomic<smart::LeaseAcquireStatus> status{
        smart::LeaseAcquireStatus::Granted};
    std::thread shutdown_waiter([&]
    {
        status.store(governor.acquire(
            exact_request(1, smart::LeaseWaitPolicy::Wait)).status);
    });
    while (governor.snapshot().pending_requests == 0)
        std::this_thread::yield();
    governor.request_shutdown();
    shutdown_waiter.join();
    require(status.load() == smart::LeaseAcquireStatus::GovernorShuttingDown,
            "shutdown did not wake waiting request");
    held_again.lease = {};
}

void bounded_bypass_and_oldest_reservation()
{
    smart::ResourceGovernor governor({3, 2, 25ms});
    auto held = governor.acquire(exact_request(2));
    require(static_cast<bool>(held), "fairness setup failed");

    std::atomic<bool> large_granted{false};
    std::thread large([&]
    {
        auto result = governor.acquire(
            exact_request(3, smart::LeaseWaitPolicy::Wait));
        large_granted.store(static_cast<bool>(result));
    });
    while (governor.snapshot().pending_requests == 0)
        std::this_thread::yield();

    for (int index = 0; index < 2; ++index)
    {
        auto small = governor.acquire(exact_request(1));
        require(static_cast<bool>(small),
                "bounded bypass should admit a fitting small request");
    }
    auto blocked_small = governor.acquire(exact_request(1));
    require(blocked_small.status == smart::LeaseAcquireStatus::WouldBlock,
            "bounded bypass did not reserve capacity for the oldest request");
    held.lease = {};
    large.join();
    require(large_granted.load(), "oldest large request starved");
    const auto snapshot = governor.snapshot();
    require(snapshot.total_bypasses == 2, "bypass telemetry mismatch");
    require(snapshot.total_oldest_reservations >= 1,
            "oldest-request reservation was not recorded");
}

void many_small_requests_do_not_starve_large_request()
{
    smart::ResourceGovernor governor({4, 4, 20ms});
    auto held = governor.acquire(exact_request(3));
    std::atomic<bool> large_done{false};
    std::thread large([&]
    {
        auto result = governor.acquire(
            exact_request(4, smart::LeaseWaitPolicy::Wait));
        large_done.store(static_cast<bool>(result));
    });
    while (governor.snapshot().pending_requests == 0)
        std::this_thread::yield();

    std::atomic<bool> stop{false};
    std::thread smalls([&]
    {
        for (int index = 0; index < 2'000 && !stop.load(); ++index)
        {
            auto result = governor.acquire(exact_request(1));
            if (result)
                std::this_thread::yield();
        }
    });
    std::this_thread::sleep_for(10ms);
    held.lease = {};
    for (int spin = 0; spin < 2'000 && !large_done.load(); ++spin)
        std::this_thread::yield();
    stop.store(true);
    smalls.join();
    large.join();
    require(large_done.load(), "large request starved behind repeated small work");
}

void concurrent_stress()
{
    constexpr std::size_t budget = 4;
    smart::ResourceGovernor governor({budget, 8, 20ms});
    std::atomic<std::size_t> participants{0};
    std::atomic<std::size_t> maximum{0};
    std::atomic<bool> failed{false};
    std::vector<std::thread> threads;
    for (std::size_t thread = 0; thread < 16; ++thread)
    {
        threads.emplace_back([&]
        {
            for (std::size_t iteration = 0; iteration < 2'000; ++iteration)
            {
                auto result = governor.acquire(
                    exact_request(1, smart::LeaseWaitPolicy::Wait));
                if (!result)
                {
                    failed.store(true);
                    return;
                }
                const auto current = participants.fetch_add(1) + 1;
                auto observed = maximum.load();
                while (current > observed
                       && !maximum.compare_exchange_weak(observed, current)) {}
                if (current > budget || governor.snapshot().active_permits > budget)
                    failed.store(true);
                participants.fetch_sub(1);
            }
        });
    }
    for (auto& thread : threads)
        thread.join();
    require(!failed.load(), "stress run exceeded governor budget");
    require(maximum.load() <= budget, "observed participation exceeded budget");
    require(governor.snapshot().active_permits == 0, "stress leaked permits");
}

void million_cycle_release_stress()
{
    constexpr std::size_t cycles = 1'000'000;
    smart::ResourceGovernor governor({1, 4, 20ms});
    const auto request = exact_request(1);
    for (std::size_t iteration = 0; iteration < cycles; ++iteration)
    {
        auto acquired = governor.acquire(request);
        require(static_cast<bool>(acquired),
                "million-cycle acquisition unexpectedly failed");
    }
    const auto snapshot = governor.snapshot();
    require(snapshot.active_permits == 0 && snapshot.active_leases == 0,
            "million-cycle stress leaked an active permit or lease");
    require(snapshot.total_grants == cycles && snapshot.total_releases == cycles,
            "million-cycle grant/release accounting mismatch");
}

void effective_capacity_reporting()
{
    const auto report = smart::effective_cpu_capacity();
    require(report.capacity >= 1, "effective CPU capacity was zero");
    require(!report.source.empty(), "effective CPU capacity source was empty");
    smart::ResourceGovernor governor({1, 2, 50ms});
    const auto snapshot = governor.snapshot();
    require(snapshot.effective_cpu_capacity == report.capacity,
            "governor did not retain the effective-capacity report");
}
} // namespace

int main()
{
    try
    {
        basic_accounting_and_inheritance();
        request_validation_and_outcomes();
        flexible_partial_grant_and_exception_release();
        direct_cancellation_notification();
        cancellation_deadline_shutdown_races();
        waiting_and_shutdown();
        bounded_bypass_and_oldest_reservation();
        many_small_requests_do_not_starve_large_request();
        concurrent_stress();
        million_cycle_release_stress();
        effective_capacity_reporting();
        std::cout << "SmartParallel v1.8 ResourceGovernor validation passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SmartParallel v1.8 ResourceGovernor validation failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
