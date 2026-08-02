#pragma once

#include <smart/runtime/runtime.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace smart::detail
{
struct RuntimeState
{
    RuntimeOptions options;
    std::shared_ptr<const Config> configuration;
    ProfileEnvironment environment;
    RuntimeFingerprint runtime_fingerprint;
    std::shared_ptr<void> adaptive_state;

    mutable std::mutex profiles_mutex;
    ProfileDatabase profiles;
    std::set<std::string> warm_started_entries;
    bool profiles_loaded_from_file = false;

    mutable std::mutex fingerprint_mutex;
    OperationExecutionFingerprint last_operation;

    mutable std::mutex resource_report_mutex;
    ResourceDecisionReport last_resource_report;

    mutable std::mutex extension_mutex;
    std::shared_ptr<void> vision_adaptive_state;

    std::atomic<std::uint64_t> operation_calls{0};
    std::atomic<std::uint64_t> deterministic_replays{0};
    std::atomic<std::uint64_t> adaptive_warm_starts{0};
    std::atomic<std::uint64_t> adaptive_cold_starts{0};
    std::atomic<std::uint64_t> learning_samples{0};
    std::atomic<std::uint64_t> timing_probes{0};
    std::atomic<std::uint64_t> holdout_probes{0};
    std::atomic<std::uint64_t> drift_probes{0};
    std::atomic<std::uint64_t> route_switches{0};
    std::atomic<std::uint64_t> profile_mutations{0};
    std::atomic<std::uint64_t> profile_file_reads_after_construction{0};
    std::atomic<std::uint64_t> profile_file_writes_from_operations{0};
    std::atomic<std::uint64_t> lease_requests{0};
    std::atomic<std::uint64_t> lease_grants{0};
    std::atomic<std::uint64_t> lease_waits{0};
    std::atomic<std::uint64_t> lease_rejections{0};
    std::atomic<std::uint64_t> nested_lease_reuses{0};
};
}
