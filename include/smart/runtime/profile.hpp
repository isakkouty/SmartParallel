#pragma once

#include <smart/core/config.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/numerical/policy.hpp>
#include <smart/runtime/resource_governor.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace smart
{
enum class ProfileStatus
{
    Candidate,
    Approved
};

enum class CompatibilityIssueCode
{
    SchemaMismatch,
    OperationMismatch,
    OperationSemanticVersionMismatch,
    DataTypeMismatch,
    NumericalPolicyMismatch,
    EvaluationOrderMismatch,
    AccumulationAlgorithmMismatch,
    CanonicalPlanMismatch,
    WorkloadExtentMismatch,
    StrideMismatch,
    LayoutMismatch,
    BoundaryModeMismatch,
    ArchitectureMismatch,
    RequiredIsaUnavailable,
    CompilerBuildMismatch,
    SchedulerUnavailable,
    ProviderUnavailable,
    ProviderVersionMismatch,
    ProviderSettingMismatch,
    WorkerBudgetMismatch,
    FloatingPointEnvironmentMismatch,
    ProfileNotApproved,
    ProfileExpired,
    IntegrityFailure,
    WorkloadFingerprintMismatch,
    BuildIdentifierMismatch
};

struct CompatibilityIssue
{
    CompatibilityIssueCode code = CompatibilityIssueCode::SchemaMismatch;
    std::string field;
    std::string expected;
    std::string actual;
    std::string message;
};

struct ProfileCompatibilityReport
{
    bool compatible = true;
    std::vector<CompatibilityIssue> issues;

    void add(CompatibilityIssue issue)
    {
        compatible = false;
        issues.push_back(std::move(issue));
    }
};

struct ProfileEvidence
{
    std::size_t sample_count = 0;
    double median_duration_ms = 0.0;
    double variability_ms = 0.0;
    double confidence = 0.0;
    bool holdout_passed = false;
    bool route_authenticated = false;
    bool numerical_capability_passed = false;
    bool correctness_passed = false;
    std::size_t route_switch_count = 0;
    std::string source_calibration_id;
    std::string created_utc;
    std::string expires_utc;
};

struct ProfileWorkloadIdentity
{
    std::vector<std::size_t> extents;
    std::vector<std::size_t> strides;
    std::string layout = "contiguous";
    std::string boundary_mode = "none";
    bool in_place = false;
    std::string semantic_constants;
    std::string exact_fingerprint;
};

struct ProfileEnvironment
{
    std::string architecture;
    std::string cpu_identity;
    std::string required_isa;
    std::size_t pointer_width = sizeof(void*) * 8;
    std::string endianness;
    std::string os_family;
    std::string compiler_identity;
    std::string compiler_version;
    std::string standard_library_identity;
    std::string build_type;
    std::string smartparallel_version;
    std::string smartparallel_build_fingerprint;
    std::string feature_macros;
    std::string tbb_version;
    std::string opencv_version;
    std::string floating_point_environment;
    std::string application_build_identifier;
};

struct OperationProfile
{
    std::string operation;
    std::string operation_semantic_version = "1.0";
    std::string element_type;
    NumericalPolicy numerical_policy = NumericalPolicy::Fast;
    std::string evaluation_order;
    std::string accumulation_algorithm;
    std::string canonical_plan = "none";
    std::string capability_requirements;
    ProfileWorkloadIdentity workload;

    std::string implementation_route;
    ExecutionPlan execution_plan;
    std::size_t exact_worker_budget = 1;
    std::string actual_worker_policy = "exact";

    // v1.8 resource contract. Old v1.7 profiles omit this block and are
    // adapted unambiguously from exact_worker_budget/execution_plan.
    bool resource_contract_present = false;
    std::size_t requested_workers = 1;
    std::size_t minimum_workers = 1;
    std::size_t preferred_workers = 1;
    std::size_t maximum_workers = 1;
    std::size_t granted_workers = 1;
    std::size_t scheduler_concurrency_cap = 1;
    std::size_t observed_participating_threads = 1;
    bool exact_grant_required = true;
    LeaseWaitPolicy lease_wait_policy = LeaseWaitPolicy::Wait;
    NestedLeaseMode nested_lease_mode = NestedLeaseMode::NotNested;
    ControlScope provider_control_scope = ControlScope::PerCall;
    ControlStrength provider_control_strength = ControlStrength::Exact;
    bool provider_serialized = false;

    std::string simd_kernel = "none";
    std::string provider = "native";
    std::string provider_version;
    std::string provider_settings;
    std::string numerical_capability;
    std::string plan_semantic_version = "1.0";

    ProfileEnvironment environment;
    ProfileEvidence evidence;
    ProfileStatus status = ProfileStatus::Candidate;
    std::string candidate_source_hash;
    std::string entry_hash;
};

struct ProfileDatabase
{
    std::size_t schema_version = 1;
    std::string semantic_version = "1.0";
    std::string smartparallel_version;
    std::string created_utc;
    ProfileEnvironment environment;
    std::vector<OperationProfile> entries;
    std::string content_hash;

    const OperationProfile* find_exact(const std::string& operation,
                                       const std::string& workload_fingerprint,
                                       NumericalPolicy policy) const noexcept;
    OperationProfile* find_exact(const std::string& operation,
                                 const std::string& workload_fingerprint,
                                 NumericalPolicy policy) noexcept;
};

using ProfileDatabaseSnapshot = ProfileDatabase;

const char* profile_status_name(ProfileStatus status) noexcept;
const char* compatibility_issue_name(CompatibilityIssueCode code) noexcept;
std::string profile_database_to_canonical_json(const ProfileDatabase& database,
                                               bool include_hash = true);
ProfileDatabase profile_database_from_json(const std::string& json);
ProfileDatabase load_profile_database(const std::filesystem::path& path);
void save_profile_database_atomic(const ProfileDatabase& database,
                                  const std::filesystem::path& path);
ProfileCompatibilityReport validate_profile_database_integrity(
    const ProfileDatabase& database);
std::string sha256_hex(const std::string& data);
}
