#include <smart/data/view.hpp>
#include <smart/linalg/operations.hpp>
#include <smart/runtime/profile.hpp>
#include <smart/runtime/runtime.hpp>
#include <smart/scientific/stencil.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
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

smart::RuntimeOptions options(std::size_t workers,
                              smart::ExecutionMode mode,
                              smart::ProfileAccess access,
                              const std::filesystem::path& profile = {})
{
    smart::RuntimeOptions value;
    value.worker_budget = workers;
    value.execution_mode = mode;
    value.profile_access = access;
    value.profile_path = profile;
    value.application_build_identifier = "v170-scientific-profile-validation";
    value.build_type = "Release";
    value.scheduler_config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    value.scheduler_config.enable_experience = false;
    value.scheduler_config.enable_experience_ranking = false;
    value.scheduler_config.enable_online_exploration = false;
    value.scheduler_config.enable_parallel_for_backend_calibration = false;
    value.scheduler_config.enable_parallel_for_auto_profiling = false;
    value.scheduler_config.enable_parallel_for_profile_cache = false;
    value.scheduler_config.enable_parallel_algorithm_hot_dispatch = false;
    value.scheduler_config.nested_min_iterations_per_worker = 1;
    value.scheduler_config.nested_min_parallel_work_ms = 0.0;
    value.scheduler_config.parallel_for_estimated_overhead_ms = 0.0;
    value.scheduler_config.parallel_for_minimum_predicted_speedup = 0.0;
    value.scheduler_config.small_workload_iteration_threshold = 0;
    value.scheduler_config.cheap_workload_sequential_threshold = 0;
    return value;
}

struct Outputs
{
    std::vector<double> axpy;
    std::vector<double> stencil;
    double dot = 0.0;
    double norm = 0.0;
    std::vector<std::string> fingerprints;
};

Outputs execute_all(smart::Runtime& runtime,
                    const std::vector<double>& x_storage,
                    const std::vector<double>& y_initial,
                    const std::vector<double>& stencil_input,
                    std::size_t vector_count,
                    std::size_t rows,
                    std::size_t columns,
                    std::size_t row_stride)
{
    Outputs result;
    result.axpy = y_initial;
    auto x = smart::data::VectorView<const double>(
        x_storage.data(), {vector_count}, {2});
    auto y = smart::data::VectorView<double>(
        result.axpy.data(), {vector_count}, {2});
    smart::linalg::axpy(runtime.context(), y, 0.75, x,
        smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
    result.fingerprints.push_back(runtime.last_operation_fingerprint().hash);

    result.dot = smart::linalg::dot(runtime.context(), x, x,
        smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
    result.fingerprints.push_back(runtime.last_operation_fingerprint().hash);
    result.norm = smart::linalg::norm(runtime.context(), x,
        smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    result.fingerprints.push_back(runtime.last_operation_fingerprint().hash);

    result.stencil.assign(rows * row_stride, -17.0);
    auto input = smart::data::MatrixView<const double>(
        stencil_input.data(), {rows, columns}, {row_stride, 1});
    auto output = smart::data::MatrixView<double>(
        result.stencil.data(), {rows, columns}, {row_stride, 1});
    smart::scientific::Stencil2DCoefficients<double> coefficients;
    coefficients.center = 0.5;
    coefficients.north = coefficients.south = coefficients.west = coefficients.east = 0.125;
    smart::scientific::stencil_2d(runtime.context(), input, output, coefficients,
        smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
    result.fingerprints.push_back(runtime.last_operation_fingerprint().hash);
    return result;
}

void approve(const std::filesystem::path& candidate,
             const std::filesystem::path& approved)
{
    auto database = smart::load_profile_database(candidate);
    require(database.entries.size() == 4, "expected four scientific Candidate profiles");
    for (auto& entry : database.entries)
    {
        require(entry.status == smart::ProfileStatus::Candidate,
                "scientific calibration silently approved a profile");
        require(entry.evidence.sample_count >= 2,
                "scientific Candidate did not collect two samples");
        require(entry.evidence.route_authenticated
                && entry.evidence.numerical_capability_passed
                && entry.evidence.correctness_passed,
                "scientific Candidate evidence is incomplete");
        entry.candidate_source_hash = entry.entry_hash;
        entry.status = smart::ProfileStatus::Approved;
    }
    smart::save_profile_database_atomic(database, approved);
}
}

int main()
{
    try
    {
        const std::size_t workers = std::min<std::size_t>(2,
            std::max<std::size_t>(1, std::thread::hardware_concurrency()));
        const auto root = std::filesystem::temp_directory_path()
            / "smartparallel-v170-scientific-profiles";
        std::error_code error;
        std::filesystem::remove_all(root, error);
        std::filesystem::create_directories(root);
        const auto candidate = root / "candidate.json";
        const auto approved = root / "approved.json";

        constexpr std::size_t vector_count = 4096;
        std::vector<double> x_storage(vector_count * 2, -3.0);
        std::vector<double> y_initial(vector_count * 2, 4.0);
        for (std::size_t i = 0; i < vector_count; ++i)
            x_storage[i * 2] = static_cast<double>((i * 31) % 211) / 19.0;

        constexpr std::size_t rows = 64;
        constexpr std::size_t columns = 61;
        constexpr std::size_t row_stride = 72;
        std::vector<double> stencil_input(rows * row_stride, 0.0);
        for (std::size_t row = 0; row < rows; ++row)
            for (std::size_t column = 0; column < columns; ++column)
                stencil_input[row * row_stride + column]
                    = static_cast<double>((row * 17 + column * 13) % 97) / 11.0;

        smart::Runtime calibration(options(workers, smart::ExecutionMode::Adaptive,
                                           smart::ProfileAccess::ReadWrite));
        (void)execute_all(calibration, x_storage, y_initial, stencil_input,
                          vector_count, rows, columns, row_stride);
        (void)execute_all(calibration, x_storage, y_initial, stencil_input,
                          vector_count, rows, columns, row_stride);
        calibration.save_profiles(candidate);
        const auto candidates = smart::load_profile_database(candidate);
        require(std::any_of(candidates.entries.begin(), candidates.entries.end(),
            [](const auto& entry) {
                return entry.operation == "smart.linalg.axpy"
                    && entry.workload.layout == "strided";
            }), "strided AXPY profile was not produced");
        require(std::any_of(candidates.entries.begin(), candidates.entries.end(),
            [](const auto& entry) {
                return entry.operation == "smart.scientific.stencil_2d"
                    && entry.workload.layout == "strided";
            }), "padded stencil profile was not produced");
        approve(candidate, approved);

        smart::Runtime replay_a(options(workers, smart::ExecutionMode::Deterministic,
                                        smart::ProfileAccess::ReadOnly, approved));
        smart::Runtime replay_b(options(workers, smart::ExecutionMode::Deterministic,
                                        smart::ProfileAccess::ReadOnly, approved));
        const Outputs a = execute_all(replay_a, x_storage, y_initial, stencil_input,
                                      vector_count, rows, columns, row_stride);
        const Outputs b = execute_all(replay_b, x_storage, y_initial, stencil_input,
                                      vector_count, rows, columns, row_stride);
        require(a.axpy == b.axpy, "strided Reproducible AXPY replay differs");
        require(a.stencil == b.stencil, "padded Reproducible stencil replay differs");
        require(std::memcmp(&a.dot, &b.dot, sizeof(double)) == 0,
                "Reproducible dot replay differs bitwise");
        require(std::memcmp(&a.norm, &b.norm, sizeof(double)) == 0,
                "Accurate norm replay differs bitwise");
        require(a.fingerprints == b.fingerprints,
                "scientific operation fingerprints differ across fresh Runtimes");
        const auto telemetry = replay_a.telemetry();
        require(telemetry.deterministic_replays == 4
                && telemetry.learning_samples == 0
                && telemetry.timing_probes == 0
                && telemetry.holdout_probes == 0
                && telemetry.drift_probes == 0
                && telemetry.route_switches == 0
                && telemetry.profile_mutations == 0,
                "scientific deterministic replay performed adaptive maintenance");

        std::filesystem::remove_all(root, error);
        std::cout << "SmartParallel v1.7 scientific profile validation passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SmartParallel v1.7 scientific profile validation failed: "
                  << error.what() << '\n';
        return 1;
    }
}
