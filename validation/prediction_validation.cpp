#include "measurement.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <smart/core/config.hpp>
#include <smart/decision/predictive_decision_model.hpp>
#include <smart/execution/executor.hpp>
#include <smart/profiling/isolated_function_profile.hpp>
#include <smart/ranking/decision_context.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_builder.hpp>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace
{
volatile long long validation_sink = 0;

double now_ms()
{
    const auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();
}

const char* strategy_name(smart::ExecutionStrategy strategy)
{
    switch (strategy)
    {
        case smart::ExecutionStrategy::Sequential:
            return "Sequential";
        case smart::ExecutionStrategy::StaticChunks:
            return "StaticChunks";
        case smart::ExecutionStrategy::DynamicChunks:
            return "DynamicChunks";
    }
    return "Unknown";
}

std::string plan_name(const smart::ExecutionPlan& plan)
{
    if (!plan.parallel || plan.strategy == smart::ExecutionStrategy::Sequential)
    {
        return "Sequential";
    }

    std::string name = std::string(smart::engine_name(plan.engine)) + "/"
                       + strategy_name(plan.strategy) + "/w" + std::to_string(plan.job_count);
    if (plan.strategy == smart::ExecutionStrategy::DynamicChunks)
    {
        name += "/c" + std::to_string(plan.chunk_size);
    }
    return name;
}

bool same_plan(const smart::ExecutionPlan& left, const smart::ExecutionPlan& right)
{
    const bool left_sequential =
        !left.parallel || left.strategy == smart::ExecutionStrategy::Sequential;
    const bool right_sequential =
        !right.parallel || right.strategy == smart::ExecutionStrategy::Sequential;

    if (left_sequential || right_sequential)
        return left_sequential && right_sequential;

    return left.engine == right.engine && left.strategy == right.strategy
           && left.parallel == right.parallel && left.job_count == right.job_count
           && left.chunk_size == right.chunk_size;
}

void consume(const std::vector<int>& values)
{
    long long sum = 0;
    for (int value : values)
    {
        sum += value;
    }
    validation_sink += sum;
}

struct CaseDefinition
{
    std::string name;
    std::size_t size;
    void (*work)(int&);
    smart::ExecutionHints hints{};
};

void cheap_work(int& value)
{
    value = value * 3 + 7;
}

void compute_work(int& value)
{
    double x = static_cast<double>(value + 1);
    for (int i = 0; i < 400; ++i)
    {
        x = std::sqrt(x + 1.0);
        x = x * 1.000001 + 0.000001;
    }
    value = static_cast<int>(x);
}

void irregular_work(int& value)
{
    // Keep this workload branch-heavy and input-dependent without using
    // floating-point domain operations. The previous implementation could
    // produce NaN and then cast it to int, which is undefined behavior and
    // terminated the MSVC calibration process at irregular_1.
    std::uint32_t x = static_cast<std::uint32_t>(value) + 0x9E3779B9u;
    const std::uint32_t loops = 20u + (x % 700u);

    for (std::uint32_t i = 0; i < loops; ++i)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;

        if (((x + i) % 7u) == 0u)
        {
            x = x * 1664525u + 1013904223u;
        }
        else if ((x & 15u) == 0u)
        {
            x = (x >> 3) | (x << 29);
        }
    }

    value = static_cast<int>(x & 0x7FFFFFFFu);
}

void mixed_work(int& value)
{
    if ((value & 15) == 0)
    {
        compute_work(value);
    }
    else
    {
        for (int i = 0; i < 30; ++i)
        {
            value = value * 1664525 + 1013904223;
        }
    }
}

void light_branch_work(int& value)
{
    int x = value;
    for (int i = 0; i < 24; ++i)
    {
        x = (x & 1) ? (x * 5 + 1) : (x / 2 + 17);
    }
    value = x;
}

void medium_compute_work(int& value)
{
    double x = static_cast<double>(value + 3);
    for (int i = 0; i < 120; ++i)
    {
        x = std::sqrt(x + 1.25) * 1.000003 + 0.000013;
    }
    value = static_cast<int>(x);
}

void heavy_compute_work(int& value)
{
    // Use the same numerically stable operation family as compute_work,
    // but with substantially more iterations.  The previous uint64 state
    // mixer triggered STATUS_INTEGER_DIVIDE_BY_ZERO in the optimized MSVC
    // validation executable before heavy_compute_1 could complete, despite
    // containing no explicit source-level division.  Keeping this workload
    // in the already validated sqrt/multiply path preserves a genuinely
    // heavier compute family without relying on the crash-triggering state
    // transformation or producing implementation-defined integer results.
    double x = static_cast<double>(value + 17);
    for (int i = 0; i < 1'600; ++i)
    {
        x = std::sqrt(x + 1.25);
        x = x * 1.000001 + 0.000001;
    }

    // x remains finite, positive, and safely representable as int.
    value = static_cast<int>(x);
}

void hash_work(int& value)
{
    unsigned int x = static_cast<unsigned int>(value);
    for (int i = 0; i < 48; ++i)
    {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
    }
    value = static_cast<int>(x & 0x7fffffffU);
}

void bursty_work(int& value)
{
    const int loops = 8 + ((value * 13) & 255);
    unsigned int x = static_cast<unsigned int>(value + 1);
    for (int i = 0; i < loops; ++i)
    {
        x = x * 1664525U + 1013904223U;
        if ((x & 31U) == 0U)
            x ^= x >> 7;
    }
    value = static_cast<int>(x & 0x7fffffffU);
}

void vector_friendly_work(int& value)
{
    unsigned int x = static_cast<unsigned int>(value);
    for (int i = 0; i < 16; ++i)
        x = x * 33U + static_cast<unsigned int>(i + 7);
    value = static_cast<int>(x & 0x7fffffffU);
}

void reset_values(std::vector<int>& values)
{
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        values[i] = static_cast<int>((i * 17 + 3) % 2048);
    }
}

struct ValidationRow
{
    std::string case_name;
    std::size_t size = 0;
    std::string predicted_plan;
    std::string measured_best_plan;
    bool winner_correct = false;
    bool statistically_tied = false;

    double prediction_confidence = 0.0;

    double predicted_best_runtime_ms = 0.0;
    double measured_best_ms = 0.0;
    double winner_prediction_error_percent = 0.0;
    double selected_plan_regret_percent = 0.0;
};

ValidationRow run_case(const CaseDefinition& definition, std::ofstream& candidates_csv)
{
    std::vector<int> values(definition.size);
    reset_values(values);

    smart::Workload workload = smart::WorkloadBuilder::container(values);
    smart::WorkloadAnalysis analysis = smart::WorkloadAnalyzer().analyze(workload);

    smart::FunctionProfiler::Config profile_config;
    profile_config.min_samples = 6;
    profile_config.max_samples = 24;
    profile_config.batch_size = 1;
    profile_config.max_batch_size = 256;
    profile_config.max_callback_invocations = 4096;
    profile_config.max_profile_time_ms = 20.0;
    profile_config.target_batch_duration_ms = 0.02;

    const smart::FunctionProfile profile =
        smart::profile_container_on_copies(values, definition.work, profile_config);

    const smart::PredictiveDecisionResult prediction = smart::PredictiveDecisionModel().predict(
        workload, analysis, &profile, definition.hints.available ? &definition.hints : nullptr);

    const smart::ranking::DecisionContext decision_context = smart::ranking::make_decision_context(
        workload.iterations, &profile, definition.hints.available ? &definition.hints : nullptr);

    if (!prediction.available)
    {
        throw std::runtime_error(std::string("Prediction unavailable for case: ")
                                 + definition.name);
    }

    struct MeasuredCandidate
    {
        smart::PlanCostEstimate estimate;
        smart::validation::MeasurementStatistics timing;
        double actual_ms = 0.0;
    };

    std::vector<MeasuredCandidate> measured;
    measured.reserve(prediction.candidates.size());
    for (const smart::PlanCostEstimate& candidate : prediction.candidates)
    {
        if (candidate.available)
            measured.push_back({candidate, {}, 0.0});
    }

    smart::validation::MeasurementConfig measurement_config;
    measurement_config.warmup_rounds = 2;
    measurement_config.measured_rounds = definition.size <= 10'000 ? 11 : 7;
    measurement_config.random_seed ^= static_cast<std::uint64_t>(definition.size);

    const auto timings = smart::validation::measure_interleaved(
        measured.size(),
        measurement_config,
        [&](std::size_t candidate_index, bool)
        {
            reset_values(values);
            const double start = now_ms();
            smart::execute_workload(workload,
                                    measured[candidate_index].estimate.plan,
                                    [&](std::size_t index)
                                    {
                                        definition.work(values[index]);
                                    });
            const double elapsed = now_ms() - start;
            consume(values);
            return elapsed;
        });

    for (std::size_t index = 0; index < measured.size(); ++index)
    {
        measured[index].timing = timings[index];
        measured[index].actual_ms = timings[index].median_ms;
    }

    const auto best_actual =
        std::min_element(measured.begin(),
                         measured.end(),
                         [](const MeasuredCandidate& left, const MeasuredCandidate& right)
                         {
                             return left.actual_ms < right.actual_ms;
                         });

    const auto predicted_winner =
        std::find_if(measured.begin(),
                     measured.end(),
                     [&](const MeasuredCandidate& item)
                     {
                         return same_plan(item.estimate.plan, prediction.recommended_plan);
                     });

    if (best_actual == measured.end() || predicted_winner == measured.end())
    {
        throw std::runtime_error("Validation candidate matching failed");
    }

    for (const MeasuredCandidate& item : measured)
    {
        const double predicted_runtime_ms =
            std::max(0.0, item.estimate.predicted_total_ms - item.estimate.framework_overhead_ms);
        const double error_percent =
            item.actual_ms > 0.0
                ? std::abs(predicted_runtime_ms - item.actual_ms) / item.actual_ms * 100.0
                : 0.0;

        candidates_csv << definition.name << ',' << definition.size << ','
                       << decision_context.logical_iterations << ','
                       << (decision_context.profile_available ? 1 : 0) << ','
                       << decision_context.profile_median_ms_per_iteration << ','
                       << decision_context.profile_coefficient_of_variation << ','
                       << decision_context.profile_tail_ratio << ','
                       << decision_context.profile_parallel_worthiness << ','
                       << decision_context.profile_regional_cost_ratio << ','
                       << (decision_context.hints.available ? 1 : 0) << ','
                       << decision_context.hints.arithmetic_intensity << ','
                       << decision_context.hints.branchiness << ','
                       << decision_context.hints.memory_randomness << ','
                       << decision_context.hints.vectorization_potential << ','
                       << decision_context.hints.dependency_depth << ','
                       << decision_context.hints.bytes_touched_per_iteration << ','
                       << decision_context.hints.external_working_set_bytes << ','
                       << decision_context.hints.feature_confidence << ','
                       << plan_name(item.estimate.plan) << ',' << item.estimate.plan.job_count
                       << ',' << predicted_runtime_ms << ',' << item.actual_ms << ','
                       << error_percent << ',' << item.estimate.confidence << ','
                       << item.estimate.predicted_execution_ms << ','
                       << item.estimate.scheduling_overhead_ms << ','
                       << item.estimate.memory_penalty_ms << ','
                       << item.estimate.imbalance_penalty_ms << ','
                       << item.timing.standard_deviation_ms << ',' << item.timing.confidence_low_ms
                       << ',' << item.timing.confidence_high_ms << ','
                       << item.estimate.analytical_baseline_total_ms << ','
                       << item.estimate.hierarchical_residual_factor << ','
                       << item.estimate.hierarchical_residual_confidence << ','
                       << item.estimate.predicted_runtime_stddev_ms << ','
                       << (item.estimate.machine_calibration_used ? 1 : 0) << ','
                       << item.estimate.machine_calibration_relative_uncertainty << ','
                       << (same_plan(item.estimate.plan, prediction.recommended_plan) ? 1 : 0)
                       << ',' << (same_plan(item.estimate.plan, best_actual->estimate.plan) ? 1 : 0)
                       << '\n';
    }

    const double winner_predicted_runtime =
        std::max(0.0,
                 predicted_winner->estimate.predicted_total_ms
                     - predicted_winner->estimate.framework_overhead_ms);

    ValidationRow row;
    row.case_name = definition.name;
    row.size = definition.size;
    row.predicted_plan = plan_name(prediction.recommended_plan);
    row.measured_best_plan = plan_name(best_actual->estimate.plan);
    row.winner_correct = same_plan(prediction.recommended_plan, best_actual->estimate.plan);
    row.statistically_tied = row.winner_correct
                             || smart::validation::confidence_intervals_overlap(
                                 predicted_winner->timing, best_actual->timing);
    row.prediction_confidence = prediction.confidence;
    row.predicted_best_runtime_ms = winner_predicted_runtime;
    row.measured_best_ms = best_actual->actual_ms;
    row.winner_prediction_error_percent =
        best_actual->actual_ms > 0.0 ? std::abs(winner_predicted_runtime - best_actual->actual_ms)
                                           / best_actual->actual_ms * 100.0
                                     : 0.0;
    row.selected_plan_regret_percent = best_actual->actual_ms > 0.0
                                           ? (predicted_winner->actual_ms - best_actual->actual_ms)
                                                 / best_actual->actual_ms * 100.0
                                           : 0.0;

    return row;
}
} // namespace

int main()
{
    // Flush every diagnostic line immediately. If validation fails, the user
    // still sees the last completed step instead of an apparently silent exit.
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    try
    {
        std::cout << "==== SmartParallel Prediction Validation ====\n";

        std::error_code filesystem_error;
        const std::filesystem::path current_directory =
            std::filesystem::current_path(filesystem_error);

        if (filesystem_error)
        {
            std::cerr << "Unable to determine the current working directory: "
                      << filesystem_error.message() << '\n';
            return 1;
        }

        const std::filesystem::path output_dir = current_directory / "validation" / "output";

        std::cout << "Working directory: " << current_directory.string() << '\n'
                  << "Output directory : " << output_dir.string() << "\n\n";

        std::filesystem::create_directories(output_dir, filesystem_error);

        if (filesystem_error)
        {
            std::cerr << "Unable to create the validation output directory: "
                      << filesystem_error.message() << '\n';
            return 1;
        }

        smart::global_config().enable_experience = false;
        smart::global_config().enable_experience_persistence = false;
        smart::global_config().enable_prediction_calibration = false;
        smart::global_config().enable_machine_runtime_calibration = true;
        smart::global_config().enable_predictive_decisions = false;
        smart::global_config().enable_predictive_shadow = true;
        smart::global_config().enable_timing_diagnostics = false;

        const std::filesystem::path candidates_path = output_dir / "prediction_candidates.csv";
        const std::filesystem::path summary_path = output_dir / "prediction_summary.csv";
        const std::filesystem::path metrics_path = output_dir / "prediction_metrics.csv";

        std::ofstream candidates_csv(candidates_path);
        std::ofstream summary_csv(summary_path);

        if (!candidates_csv.is_open())
        {
            std::cerr << "Unable to create: " << candidates_path.string() << '\n';
            return 1;
        }

        if (!summary_csv.is_open())
        {
            std::cerr << "Unable to create: " << summary_path.string() << '\n';
            return 1;
        }

        candidates_csv
            << "case,size,logical_iterations,profile_available,"
            << "profile_median_ms_per_iteration,profile_coefficient_of_variation,"
            << "profile_tail_ratio,profile_parallel_worthiness,profile_regional_cost_ratio,"
            << "hint_available,hint_arithmetic_intensity,hint_branchiness,"
            << "hint_memory_randomness,hint_vectorization_potential,hint_dependency_depth,"
            << "hint_bytes_touched_per_iteration,hint_external_working_set_bytes,"
            << "hint_feature_confidence,plan,jobs,predicted_runtime_ms,actual_ms,"
            << "absolute_error_percent,confidence,predicted_execution_ms,"
            << "scheduling_overhead_ms,memory_penalty_ms,imbalance_penalty_ms,"
            << "actual_stddev_ms,actual_ci_low_ms,actual_ci_high_ms,"
            << "analytical_baseline_ms,hierarchical_factor,"
            << "hierarchical_confidence,predicted_runtime_stddev_ms,"
            << "machine_calibration_used,machine_calibration_uncertainty,"
            << "predicted_winner,measured_winner\n";

        summary_csv << "case,size,predicted_plan,measured_best_plan,winner_correct,"
                    << "statistically_tied,prediction_confidence,predicted_best_runtime_ms,"
                       "measured_best_ms,"
                    << "winner_prediction_error_percent,selected_plan_regret_percent\n";

        smart::ExecutionHints cheap_hints;
        cheap_hints.available = true;
        cheap_hints.bytes_touched_per_iteration = sizeof(int);
        cheap_hints.vectorization_potential = 0.8;

        smart::ExecutionHints compute_hints = smart::compute_heavy();
        compute_hints.vectorization_potential = 0.4;

        smart::ExecutionHints irregular_hints = smart::compute_heavy();
        irregular_hints.branchiness = 0.9;
        irregular_hints.feature_confidence = 0.9;

        smart::ExecutionHints mixed_hints = smart::compute_heavy();
        mixed_hints.branchiness = 0.6;
        mixed_hints.bytes_touched_per_iteration = sizeof(int);

        std::vector<CaseDefinition> cases;
        cases.reserve(100);

        const auto add_family = [&cases](const std::string& prefix,
                                         const std::vector<std::size_t>& sizes,
                                         void (*work)(int&),
                                         const smart::ExecutionHints& hints)
        {
            for (std::size_t index = 0; index < sizes.size(); ++index)
            {
                cases.push_back(
                    {prefix + "_" + std::to_string(index + 1), sizes[index], work, hints});
            }
        };

        add_family(
            "cheap",
            {256, 1'000, 4'000, 16'000, 64'000, 100'000, 250'000, 500'000, 1'000'000, 2'000'000},
            cheap_work,
            cheap_hints);
        add_family(
            "vector",
            {256, 1'024, 4'096, 16'384, 65'536, 131'072, 262'144, 524'288, 1'048'576, 2'097'152},
            vector_friendly_work,
            cheap_hints);
        add_family("light_branch",
                   {128, 512, 2'048, 8'192, 32'768, 65'536, 131'072, 262'144, 524'288, 1'048'576},
                   light_branch_work,
                   irregular_hints);
        add_family("hash",
                   {128, 512, 2'048, 8'192, 32'768, 65'536, 131'072, 262'144, 524'288, 1'048'576},
                   hash_work,
                   mixed_hints);
        add_family("mixed",
                   {128, 512, 2'000, 8'000, 32'000, 64'000, 128'000, 256'000, 512'000, 1'000'000},
                   mixed_work,
                   mixed_hints);
        add_family("bursty",
                   {128, 512, 2'000, 8'000, 32'000, 64'000, 128'000, 256'000, 512'000, 1'000'000},
                   bursty_work,
                   irregular_hints);
        add_family("medium_compute",
                   {64, 256, 1'000, 4'000, 10'000, 20'000, 40'000, 60'000, 80'000, 100'000},
                   medium_compute_work,
                   compute_hints);
        add_family("compute",
                   {32, 128, 512, 1'000, 2'500, 5'000, 10'000, 25'000, 50'000, 100'000},
                   compute_work,
                   compute_hints);
        add_family("irregular",
                   {32, 128, 512, 1'000, 2'500, 5'000, 10'000, 25'000, 50'000, 100'000},
                   irregular_work,
                   irregular_hints);
        // Ten heavy-compute scales.  heavy_compute_work deliberately uses the
        // same stable arithmetic family as the successfully validated compute
        // workload, with a larger per-element operation count.
        add_family("heavy_compute",
                   {32, 64, 128, 256, 512, 1'000, 2'000, 4'000, 8'000, 16'000},
                   heavy_compute_work,
                   compute_hints);

        if (cases.size() != 100)
            throw std::runtime_error("calibration suite must contain exactly 100 workloads");

        std::vector<ValidationRow> rows;
        rows.reserve(cases.size());

        std::size_t failed_cases = 0;

        for (std::size_t index = 0; index < cases.size(); ++index)
        {
            const CaseDefinition& definition = cases[index];

            std::cout << '[' << (index + 1) << '/' << cases.size() << "] Running "
                      << definition.name << " (size=" << definition.size << ")...\n";

            try
            {
                ValidationRow row = run_case(definition, candidates_csv);

                rows.push_back(row);

                summary_csv << row.case_name << ',' << row.size << ',' << row.predicted_plan << ','
                            << row.measured_best_plan << ',' << (row.winner_correct ? 1 : 0) << ','
                            << (row.statistically_tied ? 1 : 0) << ',' << row.prediction_confidence
                            << ',' << row.predicted_best_runtime_ms << ',' << row.measured_best_ms
                            << ',' << row.winner_prediction_error_percent << ','
                            << row.selected_plan_regret_percent << '\n';

                candidates_csv.flush();
                summary_csv.flush();

                if (!candidates_csv || !summary_csv)
                {
                    throw std::runtime_error("Failed while writing validation CSV output");
                }

                std::cout << "    predicted=" << std::setw(24) << row.predicted_plan
                          << " | measured=" << std::setw(24) << row.measured_best_plan
                          << " | correct=" << (row.winner_correct ? "yes" : "no")
                          << " | error=" << std::fixed << std::setprecision(2)
                          << row.winner_prediction_error_percent << '%'
                          << " | regret=" << row.selected_plan_regret_percent << "%\n";
            }
            catch (const std::exception& exception)
            {
                ++failed_cases;
                std::cerr << "    FAILED: " << exception.what() << '\n';
            }
            catch (...)
            {
                ++failed_cases;
                std::cerr << "    FAILED: unknown exception\n";
            }
        }

        if (rows.size() != cases.size() || failed_cases != 0)
        {
            std::cerr << "\nCalibration dataset incomplete: expected " << cases.size()
                      << " successful cases, wrote " << rows.size() << ", failed " << failed_cases
                      << ".\n";
            return 2;
        }

        if (rows.empty())
        {
            std::ofstream metrics(metrics_path);
            if (metrics.is_open())
            {
                metrics << "metric,value\n";
                metrics << "cases,0\n";
                metrics << "failed_cases," << failed_cases << '\n';
            }

            std::cerr << "\nValidation produced no successful cases.\n"
                      << "Inspect the failure messages above.\n";
            return 2;
        }

        std::vector<double> errors;
        std::vector<double> regrets;
        errors.reserve(rows.size());
        regrets.reserve(rows.size());

        std::size_t correct = 0;

        for (const ValidationRow& row : rows)
        {
            errors.push_back(row.winner_prediction_error_percent);
            regrets.push_back(row.selected_plan_regret_percent);

            if (row.winner_correct)
            {
                ++correct;
            }
        }

        std::sort(errors.begin(), errors.end());
        std::sort(regrets.begin(), regrets.end());

        const double mean_error =
            std::accumulate(errors.begin(), errors.end(), 0.0) / static_cast<double>(errors.size());

        const double median_error = errors[errors.size() / 2];

        const double mean_regret = std::accumulate(regrets.begin(), regrets.end(), 0.0)
                                   / static_cast<double>(regrets.size());
        const smart::validation::RegretMetrics regret_summary =
            smart::validation::regret_metrics(regrets);
        std::size_t statistically_tied = 0;
        for (const ValidationRow& row : rows)
            statistically_tied += row.statistically_tied ? 1u : 0u;

        const double accuracy =
            static_cast<double>(correct) / static_cast<double>(rows.size()) * 100.0;

        std::ofstream metrics(metrics_path);

        if (!metrics.is_open())
        {
            std::cerr << "Unable to create: " << metrics_path.string() << '\n';
            return 1;
        }

        metrics << "metric,value\n";
        metrics << "cases," << rows.size() << '\n';
        metrics << "failed_cases," << failed_cases << '\n';
        metrics << "winner_accuracy_percent," << accuracy << '\n';
        metrics << "statistically_tied_accuracy_percent,"
                << static_cast<double>(statistically_tied) / static_cast<double>(rows.size())
                       * 100.0
                << '\n';
        metrics << "mean_prediction_error_percent," << mean_error << '\n';
        metrics << "median_prediction_error_percent," << median_error << '\n';
        metrics << "worst_prediction_error_percent," << errors.back() << '\n';
        metrics << "mean_selected_plan_regret_percent," << mean_regret << '\n';
        metrics << "median_selected_plan_regret_percent," << regret_summary.median_percent << '\n';
        metrics << "p90_selected_plan_regret_percent," << regret_summary.p90_percent << '\n';
        metrics << "worst_selected_plan_regret_percent," << regret_summary.worst_percent << '\n';
        metrics << "catastrophic_choices_over_10_percent," << regret_summary.catastrophic_10_percent
                << '\n';
        metrics << "catastrophic_choices_over_25_percent," << regret_summary.catastrophic_25_percent
                << '\n';
        metrics.flush();

        if (!metrics)
        {
            std::cerr << "Failed while writing: " << metrics_path.string() << '\n';
            return 1;
        }

        std::cout << "\nSummary\n";
        std::cout << "Successful cases: " << rows.size() << '\n';
        std::cout << "Failed cases    : " << failed_cases << '\n';
        std::cout << "Winner accuracy: " << accuracy << "%\n";
        std::cout << "Mean prediction error: " << mean_error << "%\n";
        std::cout << "Median prediction error: " << median_error << "%\n";
        std::cout << "Mean selected-plan regret: " << mean_regret << "%\n";
        std::cout << "\nResults written to:\n";
        std::cout << output_dir.string() << '\n';

        if (failed_cases != 0)
        {
            std::cerr << "\nValidation completed with failed cases.\n";
            return 2;
        }

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "\nPrediction validation failed:\n" << exception.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "\nPrediction validation failed with an unknown error.\n";
        return 1;
    }
}
