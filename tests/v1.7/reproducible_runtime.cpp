#include <smart/data/view.hpp>
#include <smart/execution/parallel.hpp>
#include <smart/execution/algorithm_dispatch.hpp>
#include <smart/profiling/function_profile_cache.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/decision/hierarchical_residual_model.hpp>
#include <smart/decision/exploration_policy.hpp>
#include <smart/decision/backend_calibration.hpp>
#include <smart/linalg/operations.hpp>
#include <smart/runtime/profile.hpp>
#include <smart/runtime/runtime.hpp>
#include <smart/scientific/stencil.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename T>
std::uint64_t bits(T value)
{
    std::uint64_t result = 0;
    static_assert(sizeof(T) <= sizeof(result));
    std::memcpy(&result, &value, sizeof(T));
    return result;
}

std::filesystem::path temp_root()
{
    const auto path = std::filesystem::temp_directory_path()
        / "smartparallel_v170_runtime_validation";
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path);
    return path;
}

smart::RuntimeOptions adaptive_options(std::size_t workers,
                                       smart::ProfileAccess access = smart::ProfileAccess::Disabled)
{
    smart::RuntimeOptions options;
    options.execution_mode = smart::ExecutionMode::Adaptive;
    options.profile_access = access;
    options.worker_budget = workers;
    options.application_build_identifier = "v170-validation";
    options.build_type = "Release";
    options.scheduler_config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    options.scheduler_config.enable_experience = false;
    options.scheduler_config.enable_experience_ranking = false;
    options.scheduler_config.enable_online_exploration = false;
    options.scheduler_config.enable_parallel_for_backend_calibration = false;
    options.scheduler_config.enable_parallel_for_auto_profiling = false;
    options.scheduler_config.enable_parallel_for_profile_cache = false;
    options.scheduler_config.enable_parallel_algorithm_hot_dispatch = false;
    options.scheduler_config.nested_min_iterations_per_worker = 1;
    options.scheduler_config.nested_min_parallel_work_ms = 0.0;
    options.scheduler_config.parallel_for_estimated_overhead_ms = 0.0;
    options.scheduler_config.parallel_for_minimum_predicted_speedup = 0.0;
    options.scheduler_config.small_workload_iteration_threshold = 0;
    options.scheduler_config.cheap_workload_sequential_threshold = 0;
    return options;
}

void run_axpy(smart::Runtime& runtime,
              std::vector<double>& y,
              const std::vector<double>& x,
              smart::NumericalPolicy policy = smart::NumericalPolicy::Reproducible)
{
    auto xv = smart::data::VectorView<const double>::contiguous(x.data(), {x.size()});
    auto yv = smart::data::VectorView<double>::contiguous(y.data(), {y.size()});
    smart::linalg::axpy(runtime.context(), yv, 1.25, xv, smart::NumericalOptions{policy});
}

std::filesystem::path create_candidate(const std::filesystem::path& root,
                                       std::size_t workers,
                                       smart::OperationExecutionFingerprint* fingerprint = nullptr)
{
    const auto path = root / "candidate.json";
    auto options = adaptive_options(workers, smart::ProfileAccess::ReadWrite);
    smart::Runtime runtime(options);
    std::vector<double> x(8192), y(8192, 3.0);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<double>(i % 97) / 17.0;
    run_axpy(runtime, y, x);
    run_axpy(runtime, y, x);
    const auto telemetry = runtime.telemetry();
    require(telemetry.adaptive_cold_starts >= 2, "cold adaptive calls were not recorded");
    require(telemetry.profile_mutations >= 2, "candidate evidence was not updated");
    const auto profiles = runtime.profiles();
    require(profiles.entries.size() == 1, "AXPY candidate was not persisted in memory");
    require(profiles.entries.front().status == smart::ProfileStatus::Candidate,
            "calibration silently approved a candidate");
    require(profiles.entries.front().evidence.sample_count >= 2,
            "candidate evidence did not accumulate samples");
    require(profiles.entries.front().evidence.holdout_passed,
            "candidate evidence did not pass the minimum holdout gate");
    runtime.save_profiles(path);
    if (fingerprint) *fingerprint = runtime.last_operation_fingerprint();
    return path;
}

std::filesystem::path approve_candidate(const std::filesystem::path& candidate,
                                        const std::filesystem::path& root)
{
    auto database = smart::load_profile_database(candidate);
    require(!database.entries.empty(), "candidate profile contains no entries");
    for (auto& entry : database.entries)
    {
        require(entry.status == smart::ProfileStatus::Candidate,
                "approval input was not Candidate");
        require(entry.evidence.sample_count >= 2, "approval sample gate failed");
        require(entry.evidence.holdout_passed, "approval holdout gate failed");
        require(entry.evidence.route_authenticated, "approval route-authentication gate failed");
        require(entry.evidence.numerical_capability_passed, "approval numerical gate failed");
        require(entry.evidence.correctness_passed, "approval correctness gate failed");
        entry.candidate_source_hash = entry.entry_hash;
        entry.status = smart::ProfileStatus::Approved;
    }
    const auto approved = root / "approved.json";
    smart::save_profile_database_atomic(database, approved);
    const auto loaded = smart::load_profile_database(approved);
    require(loaded.entries.front().status == smart::ProfileStatus::Approved,
            "explicit approval did not produce Approved profile");
    require(!loaded.entries.front().candidate_source_hash.empty(),
            "approval did not preserve Candidate source identity");
    return approved;
}


void test_runtime_construction_failures(const std::filesystem::path& root)
{
    auto expect_invalid = [](const smart::RuntimeOptions& options, const char* message)
    {
        bool rejected = false;
        try { smart::Runtime runtime(options); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, message);
    };

    smart::RuntimeOptions missing_read_only;
    missing_read_only.profile_access = smart::ProfileAccess::ReadOnly;
    expect_invalid(missing_read_only, "ReadOnly Runtime without a profile path was accepted");

    smart::RuntimeOptions deterministic_auto;
    deterministic_auto.execution_mode = smart::ExecutionMode::Deterministic;
    deterministic_auto.profile_access = smart::ProfileAccess::Disabled;
    deterministic_auto.scheduler_config.execution_engine = smart::ExecutionEngineType::Auto;
    expect_invalid(deterministic_auto, "Deterministic Runtime without a plan was accepted");

    smart::RuntimeOptions excessive_workers;
    excessive_workers.worker_budget = std::max<std::size_t>(1, std::thread::hardware_concurrency()) + 1;
    expect_invalid(excessive_workers, "Runtime accepted an impossible worker budget");

    const auto malformed = root / "malformed-construction.json";
    std::ofstream(malformed, std::ios::binary) << "{not-json";
    smart::RuntimeOptions malformed_options;
    malformed_options.profile_access = smart::ProfileAccess::ReadOnly;
    malformed_options.profile_path = malformed;
    expect_invalid(malformed_options, "Runtime accepted a malformed profile at construction");
}

struct AdaptiveStateAddresses
{
    const void* function_profiles = nullptr;
    const void* experience = nullptr;
    const void* residual = nullptr;
    const void* exploration = nullptr;
    const void* backend_calibration = nullptr;
    const void* algorithm_dispatch = nullptr;
};

AdaptiveStateAddresses adaptive_state_addresses(const smart::ExecutionContext& context)
{
    smart::detail::ExecutionContextScope scope(context);
    return {
        &smart::global_function_profile_cache(),
        &smart::global_experience_database(),
        &smart::global_hierarchical_residual_learner(),
        &smart::global_online_exploration_policy(),
        &smart::global_backend_calibration_cache(),
        &smart::detail::global_algorithm_dispatch_cache()};
}

void require_distinct_adaptive_state(const AdaptiveStateAddresses& first,
                                     const AdaptiveStateAddresses& second)
{
    require(first.function_profiles != second.function_profiles,
            "Runtime FunctionProfileCache leaked across instances");
    require(first.experience != second.experience,
            "Runtime ExperienceDatabase leaked across instances");
    require(first.residual != second.residual,
            "Runtime residual learner leaked across instances");
    require(first.exploration != second.exploration,
            "Runtime exploration policy leaked across instances");
    require(first.backend_calibration != second.backend_calibration,
            "Runtime backend calibration leaked across instances");
    require(first.algorithm_dispatch != second.algorithm_dispatch,
            "Runtime algorithm dispatch cache leaked across instances");
}

void test_runtime_construction_and_isolation()
{
    const std::size_t available = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    const std::size_t workers_a = 1;
    const std::size_t workers_b = std::min<std::size_t>(2, available);
    smart::Runtime runtime_a(adaptive_options(workers_a));
    smart::Runtime runtime_b(adaptive_options(workers_b));
    require(runtime_a.context().inherited_concurrency_budget == workers_a,
            "Runtime A worker budget was not captured");
    require(runtime_b.context().inherited_concurrency_budget == workers_b,
            "Runtime B worker budget was not captured");
    require(runtime_a.fingerprint().hash != runtime_b.fingerprint().hash || workers_a == workers_b,
            "different Runtime budgets produced one fingerprint");

    const AdaptiveStateAddresses state_a = adaptive_state_addresses(runtime_a.context());
    const AdaptiveStateAddresses state_b = adaptive_state_addresses(runtime_b.context());
    const AdaptiveStateAddresses legacy_state = adaptive_state_addresses(
        smart::default_execution_context());
    require_distinct_adaptive_state(state_a, state_b);
    require_distinct_adaptive_state(state_a, legacy_state);
    require_distinct_adaptive_state(state_b, legacy_state);
    const AdaptiveStateAddresses state_a_again = adaptive_state_addresses(runtime_a.context());
    require(state_a.function_profiles == state_a_again.function_profiles
            && state_a.experience == state_a_again.experience
            && state_a.algorithm_dispatch == state_a_again.algorithm_dispatch,
            "one Runtime did not retain its owned adaptive state");

    auto copied = runtime_a.context();
    auto copied_again = copied;
    std::atomic<std::size_t> count{0};
    smart::parallel_for(copied_again, std::size_t{0}, std::size_t{4096},
        [&](std::size_t) { count.fetch_add(1, std::memory_order_relaxed); });
    require(count.load() == 4096, "copied ExecutionContext did not execute exactly once");

    const auto before = runtime_a.fingerprint().hash;
    const auto saved_global = smart::global_config();
    smart::global_config().nested_root_concurrency_budget = workers_b + 3;
    require(runtime_a.fingerprint().hash == before,
            "legacy global configuration mutated an explicit Runtime");
    smart::global_config() = saved_global;

    smart::ExecutionContext live_context;
    {
        auto owned = std::make_unique<smart::Runtime>(adaptive_options(workers_a));
        live_context = owned->context();
    }
    std::vector<std::size_t> output(128, 0);
    smart::parallel_for(live_context, std::size_t{0}, output.size(),
        [&](std::size_t i) { output[i] = i + 1; });
    require(output.back() == output.size(), "live context did not retain Runtime state");

    auto accurate_options = adaptive_options(workers_a);
    accurate_options.default_numerical_policy = smart::NumericalPolicy::Accurate;
    smart::Runtime accurate_runtime(accurate_options);
    require(accurate_runtime.fingerprint().hash != runtime_a.fingerprint().hash,
            "different numerical defaults produced one Runtime fingerprint");

    std::vector<double> default_policy_values{3.0, 4.0, 12.0};
    const auto default_policy_view = smart::data::VectorView<const double>::contiguous(
        default_policy_values.data(), {default_policy_values.size()});
    const double accurate_default_norm = smart::linalg::norm(
        accurate_runtime.context(), default_policy_view);
    require(std::abs(accurate_default_norm - 13.0) < 1e-12,
            "Accurate Runtime default produced the wrong norm");
    const auto accurate_default_fingerprint = accurate_runtime.last_operation_fingerprint();
    require(accurate_default_fingerprint.numerical_policy == smart::NumericalPolicy::Accurate
            && accurate_default_fingerprint.canonical_plan == smart::detail::scaled_sumsq_plan_v1,
            "context-aware norm ignored the Runtime numerical default");

    const double fast_default_norm = smart::linalg::norm(
        runtime_a.context(), default_policy_view);
    require(std::abs(fast_default_norm - 13.0) < 1e-12,
            "Fast Runtime default produced the wrong norm");
    require(runtime_a.last_operation_fingerprint().numerical_policy
                == smart::NumericalPolicy::Fast,
            "Fast Runtime numerical default was not retained");

    const auto default_before = smart::default_runtime().fingerprint().hash;
    smart::Runtime isolated(adaptive_options(workers_b));
    require(smart::default_runtime().fingerprint().hash == default_before,
            "explicit Runtime construction mutated the default Runtime");

    std::vector<std::size_t> shared_a(4096, 0), shared_b(4096, 0), separate(4096, 0);
    std::thread first([&] {
        smart::parallel_for(runtime_a.context(), std::size_t{0}, shared_a.size(),
            [&](std::size_t i) { shared_a[i] = i + 3; });
    });
    std::thread second([&] {
        smart::parallel_for(runtime_a.context(), std::size_t{0}, shared_b.size(),
            [&](std::size_t i) { shared_b[i] = i + 5; });
    });
    std::thread third([&] {
        smart::parallel_for(runtime_b.context(), std::size_t{0}, separate.size(),
            [&](std::size_t i) { separate[i] = i + 7; });
    });
    first.join(); second.join(); third.join();
    require(shared_a.back() == shared_a.size() + 2
            && shared_b.back() == shared_b.size() + 4
            && separate.back() == separate.size() + 6,
            "concurrent calls through one or separate Runtimes were incorrect");
}

void test_persistence_warm_start_and_replay(const std::filesystem::path& root)
{
    const std::size_t workers = std::min<std::size_t>(2,
        std::max<std::size_t>(1, std::thread::hardware_concurrency()));
    smart::OperationExecutionFingerprint cold_fingerprint;
    const auto candidate = create_candidate(root, workers, &cold_fingerprint);
    const auto original_bytes = [&]
    {
        std::ifstream input(candidate, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    }();

    auto warm_options = adaptive_options(workers, smart::ProfileAccess::ReadOnly);
    warm_options.profile_path = candidate;
    smart::Runtime warm_runtime(warm_options);
    std::vector<double> x(8192), y(8192, 3.0);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<double>(i % 97) / 17.0;
    run_axpy(warm_runtime, y, x);
    const auto warm_telemetry = warm_runtime.telemetry();
    require(warm_telemetry.adaptive_warm_starts == 1,
            "compatible Candidate did not warm-start Adaptive execution");
    require(warm_telemetry.learning_samples == 0,
            "warm-start call unexpectedly relearned before execution");
    require(warm_runtime.last_operation_fingerprint().warm_start,
            "warm-start state was absent from operation fingerprint");

    std::ifstream unchanged_input(candidate, std::ios::binary);
    const std::string unchanged_bytes(std::istreambuf_iterator<char>(unchanged_input), {});
    require(unchanged_bytes == original_bytes, "ReadOnly Runtime changed its profile file");

    if (workers > 1)
    {
        auto stale_database = smart::load_profile_database(candidate);
        auto& stale_entry = stale_database.entries.front();
        stale_entry.execution_plan.parallel = false;
        stale_entry.execution_plan.engine = smart::ExecutionEngineType::Auto;
        stale_entry.execution_plan.job_count = 1;
        stale_entry.execution_plan.strategy = smart::ExecutionStrategy::Sequential;
        stale_entry.implementation_route = "sequential";
        const auto stale_path = root / "stale-candidate.json";
        const auto updated_path = root / "updated-candidate.json";
        smart::save_profile_database_atomic(stale_database, stale_path);
        auto stale_options = adaptive_options(workers, smart::ProfileAccess::ReadWrite);
        stale_options.profile_path = stale_path;
        smart::Runtime stale_runtime(stale_options);
        std::vector<double> first_y(8192, 3.0), second_y(8192, 3.0);
        run_axpy(stale_runtime, first_y, x);
        require(stale_runtime.last_operation_fingerprint().warm_start,
                "stale Candidate was not used as a one-call Adaptive warm start");
        run_axpy(stale_runtime, second_y, x);
        require(!stale_runtime.last_operation_fingerprint().warm_start,
                "stale Candidate permanently froze Adaptive execution");
        stale_runtime.save_profiles(updated_path);
        const auto updated_database = smart::load_profile_database(updated_path);
        require(updated_database.entries.front().implementation_route != "sequential",
                "Adaptive execution did not replace a stale warm-start route");
        require(updated_database.entries.front().evidence.sample_count
                    > stale_database.entries.front().evidence.sample_count,
                "Adaptive execution did not update stale evidence in memory");
    }

    bool candidate_rejected = false;
    try
    {
        auto deterministic_options = warm_options;
        deterministic_options.execution_mode = smart::ExecutionMode::Deterministic;
        smart::Runtime deterministic_candidate(deterministic_options);
        std::vector<double> candidate_y(8192, 7.0);
        const auto before = candidate_y;
        try { run_axpy(deterministic_candidate, candidate_y, x); }
        catch (const std::runtime_error&) { candidate_rejected = true; }
        require(candidate_y == before, "Candidate rejection modified destination data");
    }
    catch (...) { throw; }
    require(candidate_rejected, "Deterministic mode accepted a Candidate profile");

    const auto approved = approve_candidate(candidate, root);
    auto deterministic_options = adaptive_options(workers, smart::ProfileAccess::ReadOnly);
    deterministic_options.execution_mode = smart::ExecutionMode::Deterministic;
    deterministic_options.profile_path = approved;

    std::vector<double> y_a(8192, 3.0), y_b(8192, 3.0);
    smart::Runtime replay_a(deterministic_options);
    smart::Runtime replay_b(deterministic_options);
    run_axpy(replay_a, y_a, x);
    run_axpy(replay_b, y_b, x);
    require(std::memcmp(y_a.data(), y_b.data(), y_a.size() * sizeof(double)) == 0,
            "fresh deterministic processes/runtimes produced different Reproducible output");
    const auto fingerprint_a = replay_a.last_operation_fingerprint();
    const auto fingerprint_b = replay_b.last_operation_fingerprint();
    require(replay_a.fingerprint().hash == replay_b.fingerprint().hash,
            "compatible deterministic Runtime fingerprints differ");
    require(fingerprint_a.hash == fingerprint_b.hash,
            "compatible deterministic operation fingerprints differ");
    require(fingerprint_a.deterministic_replay && fingerprint_b.deterministic_replay,
            "deterministic replay flag missing");
    const auto telemetry = replay_a.telemetry();
    require(telemetry.deterministic_replays == 1, "deterministic replay not counted");
    require(telemetry.learning_samples == 0 && telemetry.timing_probes == 0
            && telemetry.holdout_probes == 0 && telemetry.drift_probes == 0
            && telemetry.route_switches == 0 && telemetry.profile_mutations == 0
            && telemetry.profile_file_reads_after_construction == 0
            && telemetry.profile_file_writes_from_operations == 0,
            "deterministic execution performed adaptive maintenance");

    std::vector<double> wrong_x(8193, 1.0), wrong_y(8193, 9.0);
    const auto unchanged = wrong_y;
    bool mismatch_rejected = false;
    try { run_axpy(replay_a, wrong_y, wrong_x); }
    catch (const std::runtime_error&) { mismatch_rejected = true; }
    require(mismatch_rejected, "exact workload mismatch was not rejected");
    require(wrong_y == unchanged, "workload mismatch modified destination data");

    const auto approved_before_size = std::filesystem::file_size(approved);
    { smart::Runtime no_destructor_write(deterministic_options); }
    require(std::filesystem::file_size(approved) == approved_before_size,
            "Runtime destructor wrote the profile file");
}



void test_atomic_persistence(const std::filesystem::path& root)
{
    const std::size_t workers = std::min<std::size_t>(2,
        std::max<std::size_t>(1, std::thread::hardware_concurrency()));
    const auto candidate = create_candidate(root / "atomic", workers);
    auto database = smart::load_profile_database(candidate);
    const auto destination = root / "atomic" / "accepted.json";
    smart::save_profile_database_atomic(database, destination);
    std::string original;
    {
        std::ifstream original_input(destination, std::ios::binary);
        original.assign(std::istreambuf_iterator<char>(original_input), {});
    }

    database.entries.front().evidence.confidence = 0.99;
    const std::string serialized = smart::profile_database_to_canonical_json(database, true);
    const auto temporary = destination.parent_path()
        / (destination.filename().string() + ".tmp."
           + smart::sha256_hex(serialized).substr(0, 16));
    std::filesystem::create_directory(temporary);
    bool write_failure = false;
    try { smart::save_profile_database_atomic(database, destination); }
    catch (const std::runtime_error&) { write_failure = true; }
    require(write_failure, "simulated temporary write failure was not reported");
    std::string unchanged;
    {
        std::ifstream unchanged_input(destination, std::ios::binary);
        unchanged.assign(std::istreambuf_iterator<char>(unchanged_input), {});
    }
    require(unchanged == original, "failed atomic save changed the previous valid profile");
    require(!std::filesystem::exists(temporary), "failed atomic save left a temporary file");

    smart::save_profile_database_atomic(database, destination);
    require(smart::load_profile_database(destination).entries.front().evidence.confidence == 0.99,
            "successful atomic replacement was not readable");

    const auto rename_target = root / "atomic" / "rename-target";
    std::filesystem::create_directory(rename_target);
    std::ofstream(rename_target / "keep.txt") << "keep";
    bool rename_failure = false;
    try { smart::save_profile_database_atomic(database, rename_target); }
    catch (const std::runtime_error&) { rename_failure = true; }
    require(rename_failure, "simulated atomic rename failure was not reported");
    require(std::filesystem::exists(rename_target / "keep.txt"),
            "rename failure damaged the existing destination directory");
}

void test_compatibility_rejection_matrix(const std::filesystem::path& root)
{
    const std::size_t workers = std::min<std::size_t>(2,
        std::max<std::size_t>(1, std::thread::hardware_concurrency()));
    const auto candidate = create_candidate(root / "compatibility", workers);
    const auto approved = approve_candidate(candidate, root / "compatibility");
    const auto base = smart::load_profile_database(approved);
    require(base.entries.size() == 1, "compatibility fixture profile is invalid");

    using Mutation = std::function<void(smart::OperationProfile&)>;
    const std::vector<std::pair<std::string, Mutation>> cases{
        {"operation", [](auto& entry) { entry.operation = "smart.linalg.other"; }},
        {"operation_semantic_version", [](auto& entry) { entry.operation_semantic_version = "2.0"; }},
        {"element_type", [](auto& entry) { entry.element_type = "float32"; }},
        {"numerical_policy", [](auto& entry) { entry.numerical_policy = smart::NumericalPolicy::Fast; }},
        {"evaluation_order", [](auto& entry) { entry.evaluation_order = "adaptive"; }},
        {"accumulation_algorithm", [](auto& entry) { entry.accumulation_algorithm = "native"; }},
        {"canonical_plan", [](auto& entry) { entry.canonical_plan = "canonical-plan-v999"; }},
        {"workload_extent", [](auto& entry) { entry.workload.extents.front() += 1; }},
        {"stride", [](auto& entry) { entry.workload.strides.front() += 1; }},
        {"layout", [](auto& entry) { entry.workload.layout = "strided"; }},
        {"boundary_mode", [](auto& entry) { entry.workload.boundary_mode = "fixed"; }},
        {"architecture", [](auto& entry) { entry.environment.architecture = "incompatible-architecture"; }},
        {"required_isa", [](auto& entry) { entry.environment.required_isa = "unavailable-isa"; }},
        {"compiler_build", [](auto& entry) { entry.environment.compiler_version += "-different"; }},
        {"scheduler_unavailable", [](auto& entry) {
            entry.execution_plan.parallel = true;
            entry.execution_plan.engine = smart::ExecutionEngineType::Auto;
            entry.execution_plan.job_count = entry.exact_worker_budget;
            entry.implementation_route = "native_auto";
        }},
        {"provider", [](auto& entry) { entry.provider = "unavailable-provider"; }},
        {"provider_version", [](auto& entry) { entry.provider_version = "different-version"; }},
        {"provider_setting", [](auto& entry) { entry.provider_settings = "different-setting"; }},
        {"implementation_route", [](auto& entry) { entry.implementation_route = "different-route"; }},
        {"worker_budget", [](auto& entry) { entry.exact_worker_budget += 1; }},
        {"actual_worker_policy", [](auto& entry) { entry.actual_worker_policy = "dynamic"; }},
        {"plan_semantic_version", [](auto& entry) { entry.plan_semantic_version = "2.0"; }},
        {"floating_point_environment", [](auto& entry) { entry.environment.floating_point_environment += "-different"; }},
        {"profile_not_approved", [](auto& entry) { entry.status = smart::ProfileStatus::Candidate; }},
        {"profile_expired", [](auto& entry) { entry.evidence.expires_utc = "2000-01-01T00:00:00Z"; }},
        {"application_build_identifier", [](auto& entry) { entry.environment.application_build_identifier = "other-application"; }},
    };

    const auto options = [&]
    {
        auto value = adaptive_options(workers, smart::ProfileAccess::ReadOnly);
        value.execution_mode = smart::ExecutionMode::Deterministic;
        return value;
    }();
    std::vector<double> x(8192);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<double>(i % 97) / 17.0;

    for (const auto& item : cases)
    {
        auto database = base;
        item.second(database.entries.front());
        const auto path = root / "compatibility" / (item.first + ".json");
        smart::save_profile_database_atomic(database, path);
        auto deterministic_options = options;
        deterministic_options.profile_path = path;
        smart::Runtime runtime(deterministic_options);
        std::vector<double> y(8192, 11.0);
        const auto unchanged = y;
        bool rejected = false;
        try { run_axpy(runtime, y, x); }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected || y != unchanged)
            throw std::runtime_error("compatibility mismatch was not rejected before mutation: "
                                     + item.first);
    }
}

void test_parser_integrity(const std::filesystem::path& root)
{
    const std::size_t workers = std::min<std::size_t>(2,
        std::max<std::size_t>(1, std::thread::hardware_concurrency()));
    const auto candidate = create_candidate(root / "parser", workers);
    std::ifstream input(candidate, std::ios::binary);
    std::string bytes(std::istreambuf_iterator<char>(input), {});
    require(!bytes.empty(), "candidate profile is empty");

    auto expect_rejected = [](const std::string& json, const char* message)
    {
        bool threw = false;
        try { (void)smart::profile_database_from_json(json); }
        catch (const std::exception&) { threw = true; }
        require(threw, message);
    };

    std::string changed = bytes;
    changed[changed.size() / 2] = changed[changed.size() / 2] == 'a' ? 'b' : 'a';
    expect_rejected(changed, "changed profile byte was accepted");
    expect_rejected(bytes.substr(0, bytes.size() / 2), "truncated profile was accepted");
    expect_rejected("{\"schema_version\":1,\"schema_version\":1}",
                    "duplicate JSON key was accepted");
    expect_rejected("", "empty profile was accepted");

    const auto canonical = smart::profile_database_to_canonical_json(
        smart::load_profile_database(candidate), true);
    const auto round_trip = smart::profile_database_to_canonical_json(
        smart::profile_database_from_json(canonical), true);
    require(canonical == round_trip, "canonical profile round trip was unstable");
    const auto whitespace_round_trip = smart::profile_database_to_canonical_json(
        smart::profile_database_from_json("  \n\t" + canonical + "\n"), true);
    require(canonical == whitespace_round_trip, "profile whitespace normalization was unstable");

    const auto first_member_end = canonical.find(',');
    require(first_member_end != std::string::npos,
            "canonical profile did not contain multiple members");
    const std::string reordered = "{"
        + canonical.substr(first_member_end + 1, canonical.size() - first_member_end - 2)
        + "," + canonical.substr(1, first_member_end - 1) + "}";
    const auto reordered_round_trip = smart::profile_database_to_canonical_json(
        smart::profile_database_from_json(reordered), true);
    require(canonical == reordered_round_trip,
            "equivalent reordered profile did not canonicalize consistently");

    std::string oversized_entries = canonical;
    const std::string entries_prefix = "\"entries\":[";
    const auto entries_begin = oversized_entries.find(entries_prefix);
    const auto entries_end = oversized_entries.find("],\"environment\"", entries_begin);
    require(entries_begin != std::string::npos && entries_end != std::string::npos,
            "profile entries array was not found");
    std::string excessive_array;
    excessive_array.reserve(3 * 4097);
    for (std::size_t index = 0; index < 4097; ++index)
    {
        if (index != 0) excessive_array.push_back(',');
        excessive_array += "{}";
    }
    oversized_entries.replace(entries_begin + entries_prefix.size(),
        entries_end - (entries_begin + entries_prefix.size()), excessive_array);
    expect_rejected(oversized_entries, "oversized profile entry count was accepted");

    std::string bad_schema = canonical;
    const auto schema_position = bad_schema.find("\"schema_version\":1");
    require(schema_position != std::string::npos, "schema field was not found");
    bad_schema.replace(schema_position, std::string("\"schema_version\":1").size(),
                       "\"schema_version\":2");
    expect_rejected(bad_schema, "newer profile schema was accepted");

    std::string bad_semantic = canonical;
    const auto semantic_position = bad_semantic.find("\"semantic_version\":\"1.0\"");
    require(semantic_position != std::string::npos, "semantic version field was not found");
    bad_semantic.replace(semantic_position,
        std::string("\"semantic_version\":\"1.0\"").size(),
        "\"semantic_version\":\"2.0\"");
    expect_rejected(bad_semantic, "newer profile semantic major version was accepted");
}


void test_malformed_profile_corpus()
{
#ifndef SMARTPARALLEL_V170_MALFORMED_PROFILE_DIR
    throw std::runtime_error("malformed profile corpus path is unavailable");
#else
    const std::filesystem::path corpus = SMARTPARALLEL_V170_MALFORMED_PROFILE_DIR;
    std::size_t cases = 0;
    for (const auto& item : std::filesystem::directory_iterator(corpus))
    {
        if (!item.is_regular_file()) continue;
        std::ifstream input(item.path(), std::ios::binary);
        const std::string bytes(std::istreambuf_iterator<char>(input), {});
        bool rejected = false;
        try { (void)smart::profile_database_from_json(bytes); }
        catch (const std::exception&) { rejected = true; }
        if (!rejected)
            throw std::runtime_error("malformed profile corpus case was accepted: "
                                     + item.path().filename().string());
        ++cases;
    }
    require(cases >= 10, "malformed profile corpus is incomplete");
#endif
}

void test_stencil_fingerprint_changes()
{
    const std::size_t workers = std::min<std::size_t>(2,
        std::max<std::size_t>(1, std::thread::hardware_concurrency()));
    smart::Runtime runtime(adaptive_options(workers));
    std::vector<double> input(32 * 32, 1.0), output(32 * 32, 0.0);
    auto in = smart::data::MatrixView<const double>::contiguous(input.data(), {32, 32});
    auto out = smart::data::MatrixView<double>::contiguous(output.data(), {32, 32});
    smart::scientific::Stencil2DCoefficients<double> c;
    c.center = 0.5; c.north = c.south = c.west = c.east = 0.125;
    smart::scientific::stencil_2d(runtime.context(), in, out, c,
        smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
    const auto first = runtime.last_operation_fingerprint();
    c.center = 0.4;
    smart::scientific::stencil_2d(runtime.context(), in, out, c,
        smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
    const auto second = runtime.last_operation_fingerprint();
    require(first.workload_fingerprint != second.workload_fingerprint,
            "semantic constant change did not alter workload fingerprint");
    require(first.hash != second.hash,
            "execution fingerprint ignored a meaningful operation change");
}
}

int main()
{
    try
    {
        const auto root = temp_root();
        test_runtime_construction_failures(root);
        test_runtime_construction_and_isolation();
        test_persistence_warm_start_and_replay(root);
        test_atomic_persistence(root);
        test_compatibility_rejection_matrix(root);
        test_parser_integrity(root);
        test_malformed_profile_corpus();
        test_stencil_fingerprint_changes();
        std::error_code error;
        std::filesystem::remove_all(root, error);
        std::cout << "SmartParallel v1.7 reproducible Runtime validation passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SmartParallel v1.7 validation failed: " << error.what() << '\n';
        return 1;
    }
}
