#include "tool_common.hpp"

#include <smart/execution/runtime_capabilities.hpp>
#if SMARTPARALLEL_TOOL_HAS_VISION
#include <smart/vision/threshold.hpp>
#endif

namespace
{
void usage()
{
    std::cerr << "usage:\n"
              << "  smartparallel_profile inspect PROFILE\n"
              << "  smartparallel_profile validate PROFILE\n"
              << "  smartparallel_profile approve CANDIDATE APPROVED\n"
              << "  smartparallel_profile compare PROFILE_A PROFILE_B\n";
}

void validate_environment(const smart::OperationProfile& entry,
                          const smart::ProfileEnvironment& current)
{
    auto mismatch = [](const char* field, const std::string& expected,
                       const std::string& actual)
    {
        throw std::runtime_error(std::string("approval environment mismatch: ")
                                 + field + " expected='" + expected
                                 + "' actual='" + actual + "'");
    };
    if (entry.environment.architecture != current.architecture)
        mismatch("architecture", current.architecture, entry.environment.architecture);
    if (entry.environment.pointer_width != current.pointer_width)
        throw std::runtime_error("approval environment mismatch: pointer_width");
    if (entry.environment.endianness != current.endianness)
        mismatch("endianness", current.endianness, entry.environment.endianness);
    if (entry.environment.os_family != current.os_family)
        mismatch("os_family", current.os_family, entry.environment.os_family);
    if (entry.environment.compiler_identity != current.compiler_identity
        || entry.environment.compiler_version != current.compiler_version)
        mismatch("compiler", current.compiler_identity + "-" + current.compiler_version,
                 entry.environment.compiler_identity + "-" + entry.environment.compiler_version);
    if (entry.environment.smartparallel_build_fingerprint
        != current.smartparallel_build_fingerprint)
        mismatch("SmartParallel build", current.smartparallel_build_fingerprint,
                 entry.environment.smartparallel_build_fingerprint);
    if (entry.environment.floating_point_environment
        != current.floating_point_environment)
        mismatch("floating-point environment", current.floating_point_environment,
                 entry.environment.floating_point_environment);
    if (entry.environment.application_build_identifier
        != current.application_build_identifier)
        mismatch("application build identifier", current.application_build_identifier,
                 entry.environment.application_build_identifier);
    if (entry.exact_worker_budget == 0)
        throw std::runtime_error("approval worker budget must be positive");
    if (entry.execution_plan.parallel
        && !smart::execution_backend_available(entry.execution_plan.engine))
        throw std::runtime_error("approval scheduler is unavailable on the current machine");
    if (entry.provider == "native") return;
#if SMARTPARALLEL_TOOL_HAS_VISION
    if (entry.provider == "opencv")
    {
        if (!smart::vision::opencv_available())
            throw std::runtime_error("approval requires unavailable OpenCV provider");
        if (entry.provider_version != smart::vision::opencv_version())
            throw std::runtime_error("approval OpenCV provider version mismatch");
        return;
    }
#endif
    throw std::runtime_error("approval requires an unavailable provider: " + entry.provider);
}

void validate_evidence(const smart::OperationProfile& entry)
{
    if (entry.status != smart::ProfileStatus::Candidate)
        throw std::runtime_error("approval requires a Candidate entry");
    if (entry.evidence.sample_count < 2) throw std::runtime_error("approval requires at least two samples");
    if (!entry.evidence.holdout_passed) throw std::runtime_error("approval holdout evidence failed");
    if (!entry.evidence.route_authenticated) throw std::runtime_error("approval route authentication failed");
    if (!entry.evidence.numerical_capability_passed) throw std::runtime_error("approval numerical capability failed");
    if (!entry.evidence.correctness_passed) throw std::runtime_error("approval correctness evidence failed");
    if (entry.evidence.confidence < 0.60) throw std::runtime_error("approval confidence is below 0.60");
    if (entry.evidence.source_calibration_id.empty())
        throw std::runtime_error("approval requires a source calibration identifier");
    if (entry.workload.exact_fingerprint.empty())
        throw std::runtime_error("approval requires an exact workload fingerprint");
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 3) { usage(); return 2; }
        const std::string command = argv[1];
        smart::Runtime current_runtime(smartparallel_tool::runtime_options(
            1, smart::ExecutionMode::Adaptive, smart::ProfileAccess::Disabled));
        const auto current_environment = current_runtime.profiles().environment;
        if (command == "validate")
        {
            const auto database = smart::load_profile_database(argv[2]);
            const auto report = smart::validate_profile_database_integrity(database);
            if (!report.compatible) throw std::runtime_error("profile integrity validation failed");
            for (const auto& entry : database.entries)
                validate_environment(entry, current_environment);
            std::cout << "valid compatible profile: entries=" << database.entries.size()
                      << " hash=" << database.content_hash << '\n';
            return 0;
        }
        if (command == "inspect")
        {
            const auto database = smart::load_profile_database(argv[2]);
            std::cout << "schema=" << database.schema_version
                      << " semantic_version=" << database.semantic_version
                      << " SmartParallel=" << database.smartparallel_version
                      << " entries=" << database.entries.size()
                      << " hash=" << database.content_hash << '\n';
            for (const auto& entry : database.entries)
            {
                bool current_compatible = true;
                std::string compatibility_reason;
                try { validate_environment(entry, current_environment); }
                catch (const std::exception& error)
                {
                    current_compatible = false;
                    compatibility_reason = error.what();
                }
                std::cout << entry.operation << " status=" << smart::profile_status_name(entry.status)
                          << " policy=" << smart::numerical_policy_name(entry.numerical_policy)
                          << " route=" << entry.implementation_route
                          << " scheduler=" << smart::runtime_name(entry.execution_plan.engine)
                          << " workers=" << entry.exact_worker_budget
                          << " samples=" << entry.evidence.sample_count
                          << " current_machine_compatible=" << (current_compatible ? "true" : "false")
                          << " workload=" << entry.workload.exact_fingerprint
                          << " entry_hash=" << entry.entry_hash;
                if (!current_compatible) std::cout << " reason=\"" << compatibility_reason << "\"";
                std::cout << '\n';
            }
            return 0;
        }
        if (command == "approve")
        {
            if (argc != 4) { usage(); return 2; }
            auto database = smart::load_profile_database(argv[2]);
            if (database.entries.empty()) throw std::runtime_error("Candidate profile contains no entries");
            for (auto& entry : database.entries)
            {
                validate_evidence(entry);
                validate_environment(entry, current_environment);
                entry.candidate_source_hash = entry.entry_hash;
                entry.status = smart::ProfileStatus::Approved;
            }
            smart::save_profile_database_atomic(database, argv[3]);
            const auto approved = smart::load_profile_database(argv[3]);
            for (const auto& entry : approved.entries)
                if (entry.status != smart::ProfileStatus::Approved)
                    throw std::runtime_error("approval post-validation failed");
            std::cout << "approved entries=" << approved.entries.size()
                      << " hash=" << approved.content_hash << '\n';
            return 0;
        }
        if (command == "compare")
        {
            if (argc != 4) { usage(); return 2; }
            const auto a = smart::load_profile_database(argv[2]);
            const auto b = smart::load_profile_database(argv[3]);
            std::cout << "database_hash_equal=" << (a.content_hash == b.content_hash ? "true" : "false")
                      << " entries_a=" << a.entries.size() << " entries_b=" << b.entries.size() << '\n';
            const std::size_t count = std::min(a.entries.size(), b.entries.size());
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto& left = a.entries[i]; const auto& right = b.entries[i];
                std::cout << "entry[" << i << "] operation_equal=" << (left.operation == right.operation)
                          << " workload_equal=" << (left.workload.exact_fingerprint == right.workload.exact_fingerprint)
                          << " route_equal=" << (left.implementation_route == right.implementation_route)
                          << " scheduler_equal=" << (left.execution_plan.engine == right.execution_plan.engine)
                          << " workers_equal=" << (left.exact_worker_budget == right.exact_worker_budget)
                          << " numerical_equal=" << (left.numerical_policy == right.numerical_policy)
                          << " build_equal=" << (left.environment.smartparallel_build_fingerprint == right.environment.smartparallel_build_fingerprint)
                          << '\n';
            }
            return a.content_hash == b.content_hash ? 0 : 1;
        }
        usage(); return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "smartparallel_profile: " << error.what() << '\n';
        return 1;
    }
}
