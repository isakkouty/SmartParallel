#include <smart/core/config.hpp>
#include <smart/decision/predictive_decision_model.hpp>
#include <smart/execution/executor.hpp>
#include <smart/profiling/isolated_function_profile.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_builder.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
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
        return std::chrono::duration<double, std::milli>(
            now.time_since_epoch()).count();
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

        std::string name = std::string(smart::engine_name(plan.engine)) + "/" +
            strategy_name(plan.strategy) +
            "/w" + std::to_string(plan.job_count);
        if (plan.strategy == smart::ExecutionStrategy::DynamicChunks)
        {
            name += "/c" + std::to_string(plan.chunk_size);
        }
        return name;
    }

    bool same_plan(
        const smart::ExecutionPlan& left,
        const smart::ExecutionPlan& right)
    {
        const bool left_sequential =
            !left.parallel ||
            left.strategy == smart::ExecutionStrategy::Sequential;
        const bool right_sequential =
            !right.parallel ||
            right.strategy == smart::ExecutionStrategy::Sequential;

        if (left_sequential || right_sequential)
            return left_sequential && right_sequential;

        return left.engine == right.engine &&
            left.strategy == right.strategy &&
            left.parallel == right.parallel &&
            left.job_count == right.job_count &&
            left.chunk_size == right.chunk_size;
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
        const char* name;
        std::size_t size;
        void (*work)(int&);
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
        const int loops = 20 + (value % 700);
        double x = static_cast<double>(value + 1);
        for (int i = 0; i < loops; ++i)
        {
            x = std::sqrt(x + 1.0);
            if ((i + value) % 7 == 0)
            {
                x = std::sin(x) + std::cos(x);
            }
        }
        value = static_cast<int>(x);
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

    void reset_values(std::vector<int>& values)
    {
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            values[i] = static_cast<int>((i * 17 + 3) % 2048);
        }
    }

    double measure_plan(
        const smart::Workload& workload,
        const smart::ExecutionPlan& plan,
        std::vector<int>& values,
        void (*work)(int&),
        int runs)
    {
        // One untimed warm-up keeps backend initialization out of the sample.
        reset_values(values);
        smart::execute_workload(workload, plan, [&](std::size_t index)
        {
            work(values[index]);
        });
        consume(values);

        std::vector<double> samples;
        samples.reserve(static_cast<std::size_t>(runs));

        for (int run = 0; run < runs; ++run)
        {
            reset_values(values);
            const double start = now_ms();
            smart::execute_workload(workload, plan, [&](std::size_t index)
            {
                work(values[index]);
            });
            samples.push_back(now_ms() - start);
            consume(values);
        }

        std::sort(samples.begin(), samples.end());
        return samples[samples.size() / 2];
    }

    struct ValidationRow
    {
        std::string case_name;
        std::size_t size = 0;
        std::string predicted_plan;
        std::string measured_best_plan;
        bool winner_correct = false;
        double prediction_confidence = 0.0;
        double predicted_best_runtime_ms = 0.0;
        double measured_best_ms = 0.0;
        double winner_prediction_error_percent = 0.0;
        double selected_plan_regret_percent = 0.0;
    };

    ValidationRow run_case(
        const CaseDefinition& definition,
        std::ofstream& candidates_csv)
    {
        std::vector<int> values(definition.size);
        reset_values(values);

        smart::Workload workload = smart::WorkloadBuilder::container(values);
        smart::WorkloadAnalysis analysis =
            smart::WorkloadAnalyzer().analyze(workload);

        smart::FunctionProfiler::Config profile_config;
        profile_config.min_samples = 6;
        profile_config.max_samples = 24;
        profile_config.batch_size = 1;
        profile_config.max_batch_size = 256;
        profile_config.max_callback_invocations = 4096;
        profile_config.max_profile_time_ms = 20.0;
        profile_config.target_batch_duration_ms = 0.02;

        const smart::FunctionProfile profile =
            smart::profile_container_on_copies(
                values,
                definition.work,
                profile_config);

        const smart::PredictiveDecisionResult prediction =
            smart::PredictiveDecisionModel().predict(
                workload,
                analysis,
                &profile);

        if (!prediction.available)
        {
            throw std::runtime_error(
                std::string("Prediction unavailable for case: ") +
                definition.name);
        }

        struct MeasuredCandidate
        {
            smart::PlanCostEstimate estimate;
            double actual_ms = 0.0;
        };

        std::vector<MeasuredCandidate> measured;
        measured.reserve(prediction.candidates.size());

        const int runs = definition.size <= 10'000 ? 9 : 5;

        for (const smart::PlanCostEstimate& candidate : prediction.candidates)
        {
            if (!candidate.available)
            {
                continue;
            }

            MeasuredCandidate item;
            item.estimate = candidate;
            item.actual_ms = measure_plan(
                workload,
                candidate.plan,
                values,
                definition.work,
                runs);
            measured.push_back(item);
        }

        const auto best_actual = std::min_element(
            measured.begin(), measured.end(),
            [](const MeasuredCandidate& left, const MeasuredCandidate& right)
            {
                return left.actual_ms < right.actual_ms;
            });

        const auto predicted_winner = std::find_if(
            measured.begin(), measured.end(),
            [&](const MeasuredCandidate& item)
            {
                return same_plan(
                    item.estimate.plan,
                    prediction.recommended_plan);
            });

        if (best_actual == measured.end() || predicted_winner == measured.end())
        {
            throw std::runtime_error("Validation candidate matching failed");
        }

        for (const MeasuredCandidate& item : measured)
        {
            const double predicted_runtime_ms = std::max(
                0.0,
                item.estimate.predicted_total_ms -
                    item.estimate.framework_overhead_ms);
            const double error_percent = item.actual_ms > 0.0
                ? std::abs(predicted_runtime_ms - item.actual_ms) /
                    item.actual_ms * 100.0
                : 0.0;

            candidates_csv
                << definition.name << ','
                << definition.size << ','
                << plan_name(item.estimate.plan) << ','
                << item.estimate.plan.job_count << ','
                << predicted_runtime_ms << ','
                << item.actual_ms << ','
                << error_percent << ','
                << item.estimate.confidence << ','
                << item.estimate.predicted_execution_ms << ','
                << item.estimate.scheduling_overhead_ms << ','
                << item.estimate.memory_penalty_ms << ','
                << item.estimate.imbalance_penalty_ms << ','
                << (item.estimate.machine_calibration_used ? 1 : 0) << ','
                << (same_plan(item.estimate.plan,
                                     prediction.recommended_plan) ? 1 : 0) << ','
                << (same_plan(item.estimate.plan,
                                     best_actual->estimate.plan) ? 1 : 0)
                << '\n';
        }

        const double winner_predicted_runtime = std::max(
            0.0,
            predicted_winner->estimate.predicted_total_ms -
                predicted_winner->estimate.framework_overhead_ms);

        ValidationRow row;
        row.case_name = definition.name;
        row.size = definition.size;
        row.predicted_plan = plan_name(prediction.recommended_plan);
        row.measured_best_plan = plan_name(best_actual->estimate.plan);
        row.winner_correct = same_plan(
            prediction.recommended_plan,
            best_actual->estimate.plan);
        row.prediction_confidence = prediction.confidence;
        row.predicted_best_runtime_ms = winner_predicted_runtime;
        row.measured_best_ms = best_actual->actual_ms;
        row.winner_prediction_error_percent = best_actual->actual_ms > 0.0
            ? std::abs(winner_predicted_runtime - best_actual->actual_ms) /
                best_actual->actual_ms * 100.0
            : 0.0;
        row.selected_plan_regret_percent = best_actual->actual_ms > 0.0
            ? (predicted_winner->actual_ms - best_actual->actual_ms) /
                best_actual->actual_ms * 100.0
            : 0.0;

        return row;
    }
}

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
            std::cerr
                << "Unable to determine the current working directory: "
                << filesystem_error.message()
                << '\n';
            return 1;
        }

        const std::filesystem::path output_dir =
            current_directory / "validation" / "output";

        std::cout
            << "Working directory: " << current_directory.string() << '\n'
            << "Output directory : " << output_dir.string() << "\n\n";

        std::filesystem::create_directories(
            output_dir,
            filesystem_error);

        if (filesystem_error)
        {
            std::cerr
                << "Unable to create the validation output directory: "
                << filesystem_error.message()
                << '\n';
            return 1;
        }

        smart::global_config().enable_experience = false;
        smart::global_config().enable_experience_persistence = false;
        smart::global_config().enable_prediction_calibration = false;
        smart::global_config().enable_machine_runtime_calibration = true;
        smart::global_config().enable_predictive_decisions = false;
        smart::global_config().enable_predictive_shadow = true;
        smart::global_config().enable_timing_diagnostics = false;

        const std::filesystem::path candidates_path =
            output_dir / "prediction_candidates.csv";
        const std::filesystem::path summary_path =
            output_dir / "prediction_summary.csv";
        const std::filesystem::path metrics_path =
            output_dir / "prediction_metrics.csv";

        std::ofstream candidates_csv(candidates_path);
        std::ofstream summary_csv(summary_path);

        if (!candidates_csv.is_open())
        {
            std::cerr
                << "Unable to create: "
                << candidates_path.string()
                << '\n';
            return 1;
        }

        if (!summary_csv.is_open())
        {
            std::cerr
                << "Unable to create: "
                << summary_path.string()
                << '\n';
            return 1;
        }

        candidates_csv
            << "case,size,plan,jobs,predicted_runtime_ms,actual_ms,"
            << "absolute_error_percent,confidence,predicted_execution_ms,"
            << "scheduling_overhead_ms,memory_penalty_ms,imbalance_penalty_ms,"
        << "machine_calibration_used,"
            << "predicted_winner,measured_winner\n";

        summary_csv
            << "case,size,predicted_plan,measured_best_plan,winner_correct,"
            << "prediction_confidence,predicted_best_runtime_ms,measured_best_ms,"
            << "winner_prediction_error_percent,selected_plan_regret_percent\n";

        const std::vector<CaseDefinition> cases =
        {
            {"cheap_1k", 1'000, cheap_work},
            {"cheap_100k", 100'000, cheap_work},
            {"cheap_1m", 1'000'000, cheap_work},
            {"compute_1k", 1'000, compute_work},
            {"compute_10k", 10'000, compute_work},
            {"compute_100k", 100'000, compute_work},
            {"irregular_1k", 1'000, irregular_work},
            {"irregular_10k", 10'000, irregular_work},
            {"irregular_100k", 100'000, irregular_work},
            {"mixed_1k", 1'000, mixed_work},
            {"mixed_10k", 10'000, mixed_work},
            {"mixed_100k", 100'000, mixed_work}
        };

        std::vector<ValidationRow> rows;
        rows.reserve(cases.size());

        std::size_t failed_cases = 0;

        for (std::size_t index = 0; index < cases.size(); ++index)
        {
            const CaseDefinition& definition = cases[index];

            std::cout
                << '[' << (index + 1) << '/' << cases.size() << "] Running "
                << definition.name
                << " (size=" << definition.size << ")...\n";

            try
            {
                ValidationRow row = run_case(
                    definition,
                    candidates_csv);

                rows.push_back(row);

                summary_csv
                    << row.case_name << ','
                    << row.size << ','
                    << row.predicted_plan << ','
                    << row.measured_best_plan << ','
                    << (row.winner_correct ? 1 : 0) << ','
                    << row.prediction_confidence << ','
                    << row.predicted_best_runtime_ms << ','
                    << row.measured_best_ms << ','
                    << row.winner_prediction_error_percent << ','
                    << row.selected_plan_regret_percent << '\n';

                candidates_csv.flush();
                summary_csv.flush();

                if (!candidates_csv || !summary_csv)
                {
                    throw std::runtime_error(
                        "Failed while writing validation CSV output");
                }

                std::cout
                    << "    predicted=" << std::setw(24)
                    << row.predicted_plan
                    << " | measured=" << std::setw(24)
                    << row.measured_best_plan
                    << " | correct="
                    << (row.winner_correct ? "yes" : "no")
                    << " | error=" << std::fixed << std::setprecision(2)
                    << row.winner_prediction_error_percent << '%'
                    << " | regret="
                    << row.selected_plan_regret_percent << "%\n";
            }
            catch (const std::exception& exception)
            {
                ++failed_cases;
                std::cerr
                    << "    FAILED: "
                    << exception.what()
                    << '\n';
            }
            catch (...)
            {
                ++failed_cases;
                std::cerr
                    << "    FAILED: unknown exception\n";
            }
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

            std::cerr
                << "\nValidation produced no successful cases.\n"
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
            std::accumulate(errors.begin(), errors.end(), 0.0) /
            static_cast<double>(errors.size());

        const double median_error =
            errors[errors.size() / 2];

        const double mean_regret =
            std::accumulate(regrets.begin(), regrets.end(), 0.0) /
            static_cast<double>(regrets.size());

        const double accuracy =
            static_cast<double>(correct) /
            static_cast<double>(rows.size()) * 100.0;

        std::ofstream metrics(metrics_path);

        if (!metrics.is_open())
        {
            std::cerr
                << "Unable to create: "
                << metrics_path.string()
                << '\n';
            return 1;
        }

        metrics << "metric,value\n";
        metrics << "cases," << rows.size() << '\n';
        metrics << "failed_cases," << failed_cases << '\n';
        metrics << "winner_accuracy_percent," << accuracy << '\n';
        metrics << "mean_prediction_error_percent," << mean_error << '\n';
        metrics << "median_prediction_error_percent," << median_error << '\n';
        metrics << "worst_prediction_error_percent," << errors.back() << '\n';
        metrics << "mean_selected_plan_regret_percent," << mean_regret << '\n';
        metrics << "worst_selected_plan_regret_percent," << regrets.back() << '\n';
        metrics.flush();

        if (!metrics)
        {
            std::cerr
                << "Failed while writing: "
                << metrics_path.string()
                << '\n';
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
            std::cerr
                << "\nValidation completed with failed cases.\n";
            return 2;
        }

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "\nPrediction validation failed:\n"
            << exception.what()
            << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr
            << "\nPrediction validation failed with an unknown error.\n";
        return 1;
    }
}
