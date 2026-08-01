#include <smart/data/view.hpp>
#include <smart/linalg/operations.hpp>
#include <smart/runtime/profile.hpp>
#include <smart/runtime/runtime.hpp>
#include <smart/scientific/stencil.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

smart::RuntimeOptions options(std::size_t workers,
                              smart::ExecutionMode mode = smart::ExecutionMode::Adaptive,
                              smart::ProfileAccess access = smart::ProfileAccess::Disabled,
                              const std::filesystem::path& path = {})
{
    smart::RuntimeOptions value;
    value.worker_budget = workers;
    value.execution_mode = mode;
    value.profile_access = access;
    value.profile_path = path;
    value.application_build_identifier = "smartparallel-v170-benchmark";
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

std::vector<double> data(std::size_t count)
{
    std::vector<double> result(count);
    for (std::size_t i = 0; i < count; ++i)
        result[i] = static_cast<double>((i * 2654435761ULL) % 100003ULL) / 100003.0;
    return result;
}

template <typename F>
double time_ms(F&& function)
{
    const auto begin = Clock::now();
    function();
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

void axpy_free(std::vector<double>& y, const std::vector<double>& x)
{
    auto xv = smart::data::VectorView<const double>::contiguous(x.data(), {x.size()});
    auto yv = smart::data::VectorView<double>::contiguous(y.data(), {y.size()});
    smart::linalg::axpy(yv, 1.125, xv, smart::NumericalOptions{smart::NumericalPolicy::Fast});
}

void axpy_context(const smart::ExecutionContext& context,
                  std::vector<double>& y, const std::vector<double>& x,
                  smart::NumericalPolicy policy = smart::NumericalPolicy::Fast)
{
    auto xv = smart::data::VectorView<const double>::contiguous(x.data(), {x.size()});
    auto yv = smart::data::VectorView<double>::contiguous(y.data(), {y.size()});
    smart::linalg::axpy(context, yv, 1.125, xv, smart::NumericalOptions{policy});
}

void approve(const std::filesystem::path& candidate, const std::filesystem::path& approved)
{
    auto database = smart::load_profile_database(candidate);
    for (auto& entry : database.entries)
    {
        require(entry.evidence.sample_count >= 2, "benchmark Candidate lacks samples");
        entry.candidate_source_hash = entry.entry_hash;
        entry.status = smart::ProfileStatus::Approved;
    }
    smart::save_profile_database_atomic(database, approved);
}
}

int main(int argc, char** argv)
{
    try
    {
        const std::filesystem::path output = argc > 1 ? argv[1] : "v170_benchmark_results";
        const std::size_t repetitions = argc > 2 ? static_cast<std::size_t>(std::stoull(argv[2])) : 15;
        const std::size_t workers = std::min<std::size_t>(4,
            std::max<std::size_t>(1, std::thread::hardware_concurrency()));
        std::filesystem::create_directories(output);
        std::ofstream raw(output / "raw.csv", std::ios::trunc);
        raw << "schema_version,benchmark,variant,repetition,value,unit\n";

        const auto x = data(1048576);
        std::vector<double> y(x.size(), 0.25);
        auto runtime_options = options(workers);
        smart::Runtime runtime(runtime_options);
        const auto context = runtime.context();
        const auto copied_context = context;

        auto& legacy = smart::global_config();
        const auto saved = legacy;
        legacy = runtime_options.scheduler_config;
        legacy.nested_root_concurrency_budget = workers;
        std::vector<double> free_y(x.size(), 0.25);
        std::vector<double> runtime_y(x.size(), 0.25);
        std::vector<double> context_y(x.size(), 0.25);
        axpy_free(free_y, x);
        axpy_context(runtime.context(), runtime_y, x);
        axpy_context(copied_context, context_y, x);
        for (std::size_t repeat = 0; repeat < repetitions; ++repeat)
        {
            double free_ms = 0.0;
            double runtime_ms = 0.0;
            double context_ms = 0.0;
            const auto run_free = [&] { free_ms = time_ms([&] { axpy_free(free_y, x); }); };
            const auto run_runtime = [&] {
                runtime_ms = time_ms([&] { axpy_context(runtime.context(), runtime_y, x); });
            };
            const auto run_context = [&] {
                context_ms = time_ms([&] { axpy_context(copied_context, context_y, x); });
            };
            switch (repeat % 3)
            {
            case 0: run_free(); run_runtime(); run_context(); break;
            case 1: run_runtime(); run_context(); run_free(); break;
            default: run_context(); run_free(); run_runtime(); break;
            }
            raw << "1,api_overhead,free_function," << repeat << ',' << free_ms << ",ms\n"
                << "1,api_overhead,explicit_runtime," << repeat << ',' << runtime_ms << ",ms\n"
                << "1,api_overhead,copied_context," << repeat << ',' << context_ms << ",ms\n";
        }
        legacy = saved;

        const auto candidate = output / "candidate_profile.json";
        const auto approved = output / "approved_profile.json";
        smart::Runtime calibration(options(workers, smart::ExecutionMode::Adaptive,
                                           smart::ProfileAccess::ReadWrite));
        std::vector<double> calibration_y(x.size(), 0.25);
        // Build evidence separately from the startup benchmark.  A cold-start sample
        // must come from a fresh Runtime; repeatedly timing one calibration Runtime
        // measures an increasingly warm process-local state and is not a valid
        // cold-versus-warm pair on every platform.
        for (std::size_t repeat = 0; repeat < 3; ++repeat)
        {
            (void)time_ms([&] {
                axpy_context(calibration.context(), calibration_y, x,
                             smart::NumericalPolicy::Reproducible);
            });
        }
        auto calibration_x = smart::data::VectorView<const double>::contiguous(
            x.data(), {x.size()});
        for (std::size_t repeat = 0; repeat < 2; ++repeat)
        {
            const double dot_ms = time_ms([&] {
                (void)smart::linalg::dot(calibration.context(), calibration_x, calibration_x,
                    smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
            });
            raw << "1,calibration,dot," << repeat << ',' << dot_ms << ",ms\n";
            const double norm_ms = time_ms([&] {
                (void)smart::linalg::norm(calibration.context(), calibration_x,
                    smart::NumericalOptions{smart::NumericalPolicy::Accurate});
            });
            raw << "1,calibration,norm," << repeat << ',' << norm_ms << ",ms\n";
        }
        constexpr std::size_t stencil_rows = 256;
        constexpr std::size_t stencil_columns = 256;
        std::vector<double> stencil_input = data(stencil_rows * stencil_columns);
        std::vector<double> stencil_output(stencil_input.size(), 0.0);
        smart::scientific::Stencil2DCoefficients<double> coefficients;
        coefficients.center = 0.5;
        coefficients.north = coefficients.south = coefficients.west = coefficients.east = 0.125;
        for (std::size_t repeat = 0; repeat < 2; ++repeat)
        {
            auto input = smart::data::MatrixView<const double>::contiguous(
                stencil_input.data(), {stencil_rows, stencil_columns});
            auto output_view = smart::data::MatrixView<double>::contiguous(
                stencil_output.data(), {stencil_rows, stencil_columns});
            const double stencil_ms = time_ms([&] {
                smart::scientific::stencil_2d(calibration.context(), input, output_view,
                    coefficients, smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
            });
            raw << "1,calibration,stencil_2d," << repeat << ',' << stencil_ms << ",ms\n";
        }
        calibration.save_profiles(candidate);
        approve(candidate, approved);

        // Warm the shared backend once outside the measured startup paths.  Each
        // measured cold, warm, and deterministic operation still receives a fresh
        // Runtime, and the order rotates to prevent a fixed first/last bias.
        {
            smart::Runtime backend_warmup(options(workers));
            std::vector<double> backend_y(x.size(), 0.25);
            axpy_context(backend_warmup.context(), backend_y, x,
                         smart::NumericalPolicy::Reproducible);
        }

        for (std::size_t repeat = 0; repeat < repetitions; ++repeat)
        {
            smart::Runtime cold(options(workers, smart::ExecutionMode::Adaptive,
                                        smart::ProfileAccess::ReadWrite));
            smart::Runtime warm(options(workers, smart::ExecutionMode::Adaptive,
                                        smart::ProfileAccess::ReadOnly, candidate));
            smart::Runtime deterministic(options(workers, smart::ExecutionMode::Deterministic,
                                                 smart::ProfileAccess::ReadOnly, approved));
            std::vector<double> cold_y(x.size(), 0.25);
            std::vector<double> warm_y(x.size(), 0.25);
            std::vector<double> deterministic_y(x.size(), 0.25);
            double cold_ms = 0.0;
            double warm_ms = 0.0;
            double deterministic_ms = 0.0;

            const auto run_cold = [&] {
                cold_ms = time_ms([&] {
                    axpy_context(cold.context(), cold_y, x,
                                 smart::NumericalPolicy::Reproducible);
                });
            };
            const auto run_warm = [&] {
                warm_ms = time_ms([&] {
                    axpy_context(warm.context(), warm_y, x,
                                 smart::NumericalPolicy::Reproducible);
                });
            };
            const auto run_deterministic = [&] {
                deterministic_ms = time_ms([&] {
                    axpy_context(deterministic.context(), deterministic_y, x,
                                 smart::NumericalPolicy::Reproducible);
                });
            };

            switch (repeat % 3)
            {
            case 0: run_cold(); run_warm(); run_deterministic(); break;
            case 1: run_warm(); run_deterministic(); run_cold(); break;
            default: run_deterministic(); run_cold(); run_warm(); break;
            }

            const auto cold_telemetry = cold.telemetry();
            require(!cold.last_operation_fingerprint().warm_start,
                    "cold benchmark unexpectedly warm-started");
            require(cold_telemetry.adaptive_cold_starts >= 1
                    && cold_telemetry.profile_mutations >= 1,
                    "cold benchmark did not exercise Adaptive evidence creation");
            require(warm.last_operation_fingerprint().warm_start,
                    "warm benchmark did not warm-start");
            const auto telemetry = deterministic.telemetry();
            require(telemetry.learning_samples == 0 && telemetry.timing_probes == 0
                    && telemetry.profile_mutations == 0,
                    "deterministic benchmark observed adaptive maintenance");
            require(deterministic.last_operation_fingerprint().deterministic_replay,
                    "deterministic benchmark did not replay an Approved profile");
            require(cold_y == warm_y && warm_y == deterministic_y,
                    "startup benchmark variants produced different outputs");

            raw << "1,startup,adaptive_cold," << repeat << ',' << cold_ms << ",ms\n"
                << "1,startup,adaptive_warm," << repeat << ',' << warm_ms << ",ms\n"
                << "1,startup,deterministic," << repeat << ',' << deterministic_ms << ",ms\n";

            {
                smart::Runtime dot_warm(options(workers, smart::ExecutionMode::Adaptive,
                                                smart::ProfileAccess::ReadOnly, candidate));
                const double dot_ms = time_ms([&] {
                    (void)smart::linalg::dot(dot_warm.context(), calibration_x, calibration_x,
                        smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
                });
                require(dot_warm.last_operation_fingerprint().warm_start,
                        "dot warm benchmark did not warm-start");
                raw << "1,warm_start_operation,dot," << repeat << ',' << dot_ms << ",ms\n";
            }
            {
                smart::Runtime norm_warm(options(workers, smart::ExecutionMode::Adaptive,
                                                 smart::ProfileAccess::ReadOnly, candidate));
                const double norm_ms = time_ms([&] {
                    (void)smart::linalg::norm(norm_warm.context(), calibration_x,
                        smart::NumericalOptions{smart::NumericalPolicy::Accurate});
                });
                require(norm_warm.last_operation_fingerprint().warm_start,
                        "norm warm benchmark did not warm-start");
                raw << "1,warm_start_operation,norm," << repeat << ',' << norm_ms << ",ms\n";
            }
            {
                smart::Runtime stencil_warm(options(workers, smart::ExecutionMode::Adaptive,
                                                    smart::ProfileAccess::ReadOnly, candidate));
                auto input = smart::data::MatrixView<const double>::contiguous(
                    stencil_input.data(), {stencil_rows, stencil_columns});
                auto output_view = smart::data::MatrixView<double>::contiguous(
                    stencil_output.data(), {stencil_rows, stencil_columns});
                const double stencil_ms = time_ms([&] {
                    smart::scientific::stencil_2d(stencil_warm.context(), input, output_view,
                        coefficients, smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
                });
                require(stencil_warm.last_operation_fingerprint().warm_start,
                        "stencil warm benchmark did not warm-start");
                raw << "1,warm_start_operation,stencil_2d," << repeat << ',' << stencil_ms << ",ms\n";
            }
        }

        const auto base = smart::load_profile_database(candidate);
        for (std::size_t entry_count : {std::size_t{1}, std::size_t{10}, std::size_t{100}, std::size_t{1000}})
        {
            auto database = base;
            database.entries.clear();
            for (std::size_t index = 0; index < entry_count; ++index)
            {
                auto entry = base.entries.front();
                entry.operation = "smart.benchmark.synthetic_" + std::to_string(index);
                entry.workload.exact_fingerprint = smart::sha256_hex(entry.operation);
                entry.entry_hash.clear();
                database.entries.push_back(std::move(entry));
            }
            const auto path = output / ("profile_" + std::to_string(entry_count) + ".json");
            smart::save_profile_database_atomic(database, path);
            for (std::size_t repeat = 0; repeat < repetitions; ++repeat)
            {
                const double elapsed = time_ms([&] { (void)smart::load_profile_database(path); });
                raw << "1,profile_load," << entry_count << ',' << repeat << ',' << elapsed << ",ms\n";
            }
        }

        for (std::size_t repeat = 0; repeat < repetitions; ++repeat)
        {
            const double no_profile = time_ms([&] { smart::Runtime constructed(options(workers)); });
            const double with_profile = time_ms([&] {
                smart::Runtime constructed(options(workers, smart::ExecutionMode::Adaptive,
                                                   smart::ProfileAccess::ReadOnly, candidate));
            });
            raw << "1,runtime_construction,no_profile," << repeat << ',' << no_profile << ",ms\n"
                << "1,runtime_construction,small_profile," << repeat << ',' << with_profile << ",ms\n";
        }
        raw.close();
        std::cout << "SmartParallel v1.7 benchmark raw evidence: " << (output / "raw.csv") << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SmartParallel v1.7 benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
