#pragma once

#include <smart/core/config.hpp>
#include <smart/execution/execution_context.hpp>
#include <smart/numerical/policy.hpp>
#include <smart/runtime/profile.hpp>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace smart
{
enum class ExecutionMode
{
    Adaptive,
    Deterministic
};

enum class ProfileAccess
{
    Disabled,
    ReadOnly,
    ReadWrite
};

struct RuntimeOptions
{
    ExecutionMode execution_mode = ExecutionMode::Adaptive;
    ProfileAccess profile_access = ProfileAccess::Disabled;
    std::size_t worker_budget = 0;
    NumericalPolicy default_numerical_policy = NumericalPolicy::Fast;
    Config scheduler_config{};
    std::filesystem::path profile_path;
    std::string application_build_identifier;
    std::string build_type = "unknown";
};

struct RuntimeFingerprint
{
    std::string canonical_identity;
    std::string hash;
};

struct RuntimeTelemetrySnapshot
{
    std::uint64_t operation_calls = 0;
    std::uint64_t deterministic_replays = 0;
    std::uint64_t adaptive_warm_starts = 0;
    std::uint64_t adaptive_cold_starts = 0;
    std::uint64_t learning_samples = 0;
    std::uint64_t timing_probes = 0;
    std::uint64_t holdout_probes = 0;
    std::uint64_t drift_probes = 0;
    std::uint64_t route_switches = 0;
    std::uint64_t profile_mutations = 0;
    std::uint64_t profile_file_reads_after_construction = 0;
    std::uint64_t profile_file_writes_from_operations = 0;
};

struct OperationExecutionFingerprint
{
    std::string runtime_fingerprint;
    std::string operation;
    std::string operation_semantic_version;
    std::string workload_fingerprint;
    NumericalPolicy numerical_policy = NumericalPolicy::Fast;
    std::string evaluation_order;
    std::string accumulation_algorithm;
    std::string canonical_plan;
    ExecutionMode execution_mode = ExecutionMode::Adaptive;
    ProfileAccess profile_access = ProfileAccess::Disabled;
    std::string profile_database_hash;
    std::string profile_entry_hash;
    ProfileStatus profile_status = ProfileStatus::Candidate;
    std::string selected_route;
    ExecutionEngineType selected_scheduler = ExecutionEngineType::Auto;
    std::size_t worker_budget = 1;
    std::size_t actual_worker_count = 1;
    std::string simd_kernel;
    std::string provider;
    std::string provider_version;
    bool forced_route = false;
    bool warm_start = false;
    bool deterministic_replay = false;
    std::string hash;
};

namespace detail
{
struct RuntimeState;
}

class Runtime
{
  public:
    explicit Runtime(RuntimeOptions options = {});

    ExecutionContext context() const noexcept;
    const RuntimeOptions& options() const noexcept;

    void load_profiles(const std::filesystem::path& path);
    void save_profiles(const std::filesystem::path& path) const;

    ProfileDatabaseSnapshot profiles() const;
    RuntimeFingerprint fingerprint() const;
    RuntimeTelemetrySnapshot telemetry() const noexcept;
    OperationExecutionFingerprint last_operation_fingerprint() const;

  private:
    std::shared_ptr<detail::RuntimeState> state_;
};

Runtime& default_runtime();
ExecutionContext default_execution_context();
ExecutionContext implicit_execution_context();
NumericalPolicy execution_context_default_numerical_policy(
    const ExecutionContext& context) noexcept;
const char* execution_mode_name(ExecutionMode mode) noexcept;
const char* profile_access_name(ProfileAccess access) noexcept;
}
