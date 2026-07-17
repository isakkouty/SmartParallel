#include <smart/core/config.hpp>
#include <smart/decision/predictive_decision_model.hpp>
#include <smart/execution/executor.hpp>
#include <smart/profiling/isolated_function_profile.hpp>
#include <smart/ranking/decision_context.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_builder.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "measurement.hpp"

namespace
{
    volatile std::uint64_t holdout_sink = 0;
    std::vector<std::uint32_t> pointer_chain;

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
        if (!plan.parallel ||
            plan.strategy == smart::ExecutionStrategy::Sequential)
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

    smart::FunctionProfiler::Config profile_config()
    {
        smart::FunctionProfiler::Config config;
        config.min_samples = 6;
        config.max_samples = 24;
        config.batch_size = 1;
        config.max_batch_size = 256;
        config.max_callback_invocations = 4096;
        config.max_profile_time_ms = 20.0;
        config.target_batch_duration_ms = 0.02;
        return config;
    }

    template <typename Value>
    void consume_values(const std::vector<Value>& values)
    {
        std::uint64_t sum = 0;
        for (const Value& value : values)
        {
            const unsigned char* bytes =
                reinterpret_cast<const unsigned char*>(&value);
            for (std::size_t i = 0; i < sizeof(Value); ++i)
            {
                sum = sum * 131u + bytes[i];
            }
        }
        holdout_sink += sum;
    }

    struct ValidationRow
    {
        std::string suite;
        std::string case_name;
        std::size_t logical_iterations = 0;
        std::string predicted_plan;
        std::string measured_best_plan;
        bool exact_winner = false;
        bool within_tolerance = false;
        double prediction_confidence = 0.0;
        double predicted_runtime_ms = 0.0;
        double measured_best_ms = 0.0;
        double prediction_error_percent = 0.0;
        double regret_percent = 0.0;
    };

    struct MeasuredCandidate
    {
        smart::PlanCostEstimate estimate;
        smart::validation::MeasurementStatistics timing;
        double actual_ms = 0.0;
    };

    ValidationRow summarize_case(
        const std::string& suite,
        const std::string& case_name,
        std::size_t logical_iterations,
        const smart::PredictiveDecisionResult& prediction,
        const std::vector<MeasuredCandidate>& measured,
        const smart::FunctionProfile& profile,
        const smart::ExecutionHints* hints,
        std::ofstream& candidates_csv)
    {
        if (measured.empty())
        {
            throw std::runtime_error("No measurable candidates");
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

        if (best_actual == measured.end() ||
            predicted_winner == measured.end())
        {
            throw std::runtime_error("Candidate matching failed");
        }

        constexpr double near_optimal_tolerance = 0.03;
        const smart::ranking::DecisionContext decision_context =
            smart::ranking::make_decision_context(
                logical_iterations, &profile, hints);

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
                << suite << ','
                << case_name << ','
                << logical_iterations << ','
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
                << item.timing.standard_deviation_ms << ','
                << item.timing.confidence_low_ms << ','
                << item.timing.confidence_high_ms << ','
                << item.estimate.analytical_baseline_total_ms << ','
                << item.estimate.hierarchical_residual_factor << ','
                << item.estimate.hierarchical_residual_confidence << ','
                << item.estimate.predicted_runtime_stddev_ms << ','
                << (item.estimate.machine_calibration_used ? 1 : 0) << ','
                << item.estimate.machine_calibration_relative_uncertainty << ','
                << (same_plan(
                        item.estimate.plan,
                        prediction.recommended_plan) ? 1 : 0) << ','
                << (same_plan(
                        item.estimate.plan,
                        best_actual->estimate.plan) ? 1 : 0)
                << '\n';
        }

        const double predicted_runtime_ms = std::max(
            0.0,
            predicted_winner->estimate.predicted_total_ms -
                predicted_winner->estimate.framework_overhead_ms);

        ValidationRow row;
        row.suite = suite;
        row.case_name = case_name;
        row.logical_iterations = logical_iterations;
        row.predicted_plan = plan_name(prediction.recommended_plan);
        row.measured_best_plan = plan_name(best_actual->estimate.plan);
        row.exact_winner = same_plan(
            prediction.recommended_plan,
            best_actual->estimate.plan);
        row.within_tolerance =
            predicted_winner->actual_ms <=
                best_actual->actual_ms * (1.0 + near_optimal_tolerance) ||
            smart::validation::confidence_intervals_overlap(
                predicted_winner->timing, best_actual->timing);
        row.prediction_confidence = prediction.confidence;
        row.predicted_runtime_ms = predicted_runtime_ms;
        row.measured_best_ms = best_actual->actual_ms;
        row.prediction_error_percent = best_actual->actual_ms > 0.0
            ? std::abs(predicted_runtime_ms - best_actual->actual_ms) /
                best_actual->actual_ms * 100.0
            : 0.0;
        row.regret_percent = best_actual->actual_ms > 0.0
            ? (predicted_winner->actual_ms - best_actual->actual_ms) /
                best_actual->actual_ms * 100.0
            : 0.0;
        return row;
    }

    using IntWork = void (*)(int&);
    using IntReset = void (*)(std::vector<int>&);

    struct IntCase
    {
        const char* suite;
        const char* name;
        std::size_t size;
        IntWork work;
        IntReset reset;
    };

    void reset_default(std::vector<int>& values)
    {
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            values[i] = static_cast<int>((i * 17 + 3) % 2048);
        }
    }

    void reset_position(std::vector<int>& values)
    {
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            values[i] = static_cast<int>(i);
        }
    }

    void memory_stream_work(int& value)
    {
        value += 1;
    }

    void cache_resident_work(int& value)
    {
        value = (value * 33) ^ (value >> 3);
    }

    void branch_heavy_work(int& value)
    {
        std::uint32_t x = static_cast<std::uint32_t>(value + 1);
        for (int i = 0; i < 256; ++i)
        {
            if (((x + static_cast<std::uint32_t>(i)) & 7u) < 3u)
            {
                x = x * 1664525u + 1013904223u;
            }
            else
            {
                x ^= (x << 7) | (x >> 25);
            }
        }
        value = static_cast<int>(x);
    }

    void tiny_heavy_work(int& value)
    {
        double x = static_cast<double>(value + 1);
        for (int i = 0; i < 2500; ++i)
        {
            x = std::sqrt(x + 1.0);
            x = std::sin(x) + std::cos(x);
        }
        value = static_cast<int>(x);
    }

    void clustered_work(int& value)
    {
        const std::size_t position = static_cast<std::size_t>(value);
        const bool expensive_cluster =
            (position % 4096u) >= 3072u;
        const int loops = expensive_cluster ? 1800 : 80;

        double x = static_cast<double>(value + 1);
        for (int i = 0; i < loops; ++i)
        {
            x = std::sqrt(x + 1.0) * 1.000001;
        }
        value = static_cast<int>(x);
    }

    void sparse_like_work(int& value)
    {
        if (pointer_chain.empty())
        {
            return;
        }

        std::uint32_t index =
            static_cast<std::uint32_t>(value) %
            static_cast<std::uint32_t>(pointer_chain.size());
        std::uint32_t accumulator = index;

        for (int i = 0; i < 96; ++i)
        {
            index = pointer_chain[index];
            accumulator ^= index + static_cast<std::uint32_t>(i);
        }

        value = static_cast<int>(accumulator);
    }

    void prepare_pointer_chain(std::size_t size)
    {
        pointer_chain.resize(size);
        if (size == 0)
        {
            return;
        }

        constexpr std::uint64_t multiplier = 11400714819323198485ull;
        for (std::size_t i = 0; i < size; ++i)
        {
            pointer_chain[i] = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(i) * multiplier + 97u) % size);
        }
    }

    ValidationRow run_int_case(
        const IntCase& definition,
        std::ofstream& candidates_csv)
    {
        std::vector<int> values(definition.size);
        definition.reset(values);

        const smart::Workload workload =
            smart::WorkloadBuilder::container(values);
        const smart::WorkloadAnalysis analysis =
            smart::WorkloadAnalyzer().analyze(workload);
        const smart::FunctionProfile profile =
            smart::profile_container_on_copies(
                values,
                definition.work,
                profile_config());
        smart::ExecutionHints hints;
        if (std::string(definition.suite) == "sparse")
        {
            hints = smart::pointer_chasing(
                pointer_chain.size() * sizeof(std::uint32_t),
                96.0);
        }
        else if (std::string(definition.suite) == "memory")
        {
            hints.available = true;
            hints.bytes_touched_per_iteration = sizeof(int);
            hints.vectorization_potential = 0.8;
        }
        else if (std::string(definition.suite) == "cache")
        {
            hints.available = true;
            hints.bytes_touched_per_iteration = sizeof(int);
            hints.vectorization_potential = 0.6;
        }
        else if (std::string(definition.suite) == "branch")
        {
            hints.available = true;
            hints.branchiness = 0.9;
        }

        const smart::PredictiveDecisionResult prediction =
            smart::PredictiveDecisionModel().predict(
                workload,
                analysis,
                &profile,
                hints.available ? &hints : nullptr);

        if (!prediction.available)
        {
            throw std::runtime_error("Prediction unavailable");
        }

        std::vector<MeasuredCandidate> measured;
        for (const smart::PlanCostEstimate& candidate : prediction.candidates)
        {
            if (candidate.available)
                measured.push_back({candidate, {}, 0.0});
        }
        smart::validation::MeasurementConfig measurement_config;
        measurement_config.warmup_rounds = 2;
        measurement_config.measured_rounds =
            definition.size <= 10'000 ? 11 : 7;
        measurement_config.random_seed ^=
            static_cast<std::uint64_t>(definition.size);
        const auto timings = smart::validation::measure_interleaved(
            measured.size(), measurement_config,
            [&](std::size_t candidate_index, bool)
            {
                definition.reset(values);
                const double start = now_ms();
                smart::execute_workload(
                    workload, measured[candidate_index].estimate.plan,
                    [&](std::size_t index)
                    {
                        definition.work(values[index]);
                    });
                const double elapsed = now_ms() - start;
                consume_values(values);
                return elapsed;
            });
        for (std::size_t index = 0; index < measured.size(); ++index)
        {
            measured[index].timing = timings[index];
            measured[index].actual_ms = timings[index].median_ms;
        }

        return summarize_case(
            definition.suite,
            definition.name,
            workload.iterations,
            prediction,
            measured,
            profile,
            hints.available ? &hints : nullptr,
            candidates_csv);
    }

    struct LargeRecord
    {
        std::array<std::uint64_t, 16> words{};
    };

    void reset_large(std::vector<LargeRecord>& values)
    {
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            for (std::size_t j = 0; j < values[i].words.size(); ++j)
            {
                values[i].words[j] =
                    static_cast<std::uint64_t>(i * 131u + j * 17u + 3u);
            }
        }
    }

    void large_record_work(LargeRecord& record)
    {
        std::uint64_t mixed = 0;
        for (std::uint64_t& word : record.words)
        {
            word = word * 6364136223846793005ull + 1442695040888963407ull;
            mixed ^= word;
        }
        record.words[0] ^= mixed;
    }

    ValidationRow run_large_case(
        const char* name,
        std::size_t size,
        std::ofstream& candidates_csv)
    {
        std::vector<LargeRecord> values(size);
        reset_large(values);

        const smart::Workload workload =
            smart::WorkloadBuilder::container(values);
        const smart::WorkloadAnalysis analysis =
            smart::WorkloadAnalyzer().analyze(workload);
        const smart::FunctionProfile profile =
            smart::profile_container_on_copies(
                values,
                large_record_work,
                profile_config());
        const smart::PredictiveDecisionResult prediction =
            smart::PredictiveDecisionModel().predict(
                workload,
                analysis,
                &profile);

        if (!prediction.available)
        {
            throw std::runtime_error("Large-record prediction unavailable");
        }

        std::vector<MeasuredCandidate> measured;
        for (const smart::PlanCostEstimate& candidate : prediction.candidates)
        {
            if (candidate.available)
                measured.push_back({candidate, {}, 0.0});
        }
        smart::validation::MeasurementConfig measurement_config;
        measurement_config.warmup_rounds = 2;
        measurement_config.measured_rounds = size <= 20'000 ? 9 : 7;
        measurement_config.random_seed ^=
            static_cast<std::uint64_t>(size) << 1u;
        const auto timings = smart::validation::measure_interleaved(
            measured.size(), measurement_config,
            [&](std::size_t candidate_index, bool)
            {
                reset_large(values);
                const double start = now_ms();
                smart::execute_workload(
                    workload, measured[candidate_index].estimate.plan,
                    [&](std::size_t index)
                    {
                        large_record_work(values[index]);
                    });
                const double elapsed = now_ms() - start;
                consume_values(values);
                return elapsed;
            });
        for (std::size_t index = 0; index < measured.size(); ++index)
        {
            measured[index].timing = timings[index];
            measured[index].actual_ms = timings[index].median_ms;
        }

        return summarize_case(
            "large_object",
            name,
            workload.iterations,
            prediction,
            measured,
            profile,
            nullptr,
            candidates_csv);
    }

    std::uint32_t pair_kernel(int left, int right)
    {
        std::uint32_t x = static_cast<std::uint32_t>(left + 1);
        std::uint32_t y = static_cast<std::uint32_t>(right + 3);
        for (int i = 0; i < 80; ++i)
        {
            x = (x * 1664525u + 1013904223u) ^ (y >> 3);
            y = (y * 22695477u + 1u) ^ (x << 5);
        }
        return x ^ y;
    }

    void pair_profile_work(int& left, int& right)
    {
        left = static_cast<int>(pair_kernel(left, right));
    }

    void reset_pair(std::vector<int>& left, std::vector<int>& right)
    {
        reset_default(left);
        reset_default(right);
    }

    ValidationRow run_pair_case(
        const char* name,
        std::size_t left_size,
        std::size_t right_size,
        std::ofstream& candidates_csv)
    {
        std::vector<int> left(left_size);
        std::vector<int> right(right_size);
        reset_pair(left, right);

        const smart::Workload workload =
            smart::WorkloadBuilder::pair_container(left, right);
        const smart::WorkloadAnalysis analysis =
            smart::WorkloadAnalyzer().analyze(workload);
        const smart::FunctionProfile profile =
            smart::profile_pair_on_copies(
                left,
                right,
                pair_profile_work,
                profile_config());
        const smart::PredictiveDecisionResult prediction =
            smart::PredictiveDecisionModel().predict(
                workload,
                analysis,
                &profile);

        if (!prediction.available)
        {
            throw std::runtime_error("Pair prediction unavailable");
        }

        std::vector<std::uint32_t> outputs(workload.iterations, 0u);
        std::vector<MeasuredCandidate> measured;
        for (const smart::PlanCostEstimate& candidate : prediction.candidates)
        {
            if (candidate.available)
                measured.push_back({candidate, {}, 0.0});
        }
        smart::validation::MeasurementConfig measurement_config;
        measurement_config.warmup_rounds = 2;
        measurement_config.measured_rounds =
            workload.iterations <= 100'000 ? 9 : 7;
        measurement_config.random_seed ^=
            static_cast<std::uint64_t>(workload.iterations) << 2u;
        const std::size_t right_extent = right.size();
        const auto timings = smart::validation::measure_interleaved(
            measured.size(), measurement_config,
            [&](std::size_t candidate_index, bool)
            {
                std::fill(outputs.begin(), outputs.end(), 0u);
                const double start = now_ms();
                smart::execute_workload(
                    workload, measured[candidate_index].estimate.plan,
                    [&](std::size_t flat)
                    {
                        outputs[flat] = pair_kernel(
                            left[flat / right_extent],
                            right[flat % right_extent]);
                    });
                const double elapsed = now_ms() - start;
                consume_values(outputs);
                return elapsed;
            });
        for (std::size_t index = 0; index < measured.size(); ++index)
        {
            measured[index].timing = timings[index];
            measured[index].actual_ms = timings[index].median_ms;
        }

        return summarize_case(
            "pair",
            name,
            workload.iterations,
            prediction,
            measured,
            profile,
            nullptr,
            candidates_csv);
    }

    double percentile(std::vector<double> values, double fraction)
    {
        if (values.empty())
        {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        const std::size_t index = static_cast<std::size_t>(
            fraction * static_cast<double>(values.size() - 1));
        return values[index];
    }
}

int main()
{
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    try
    {
        std::error_code error;
        const std::filesystem::path root =
            std::filesystem::current_path(error);
        if (error)
        {
            std::cerr << "Unable to determine working directory: "
                      << error.message() << '\n';
            return 1;
        }

        const std::filesystem::path output_dir =
            root / "validation" / "output";
        std::filesystem::create_directories(output_dir, error);
        if (error)
        {
            std::cerr << "Unable to create output directory: "
                      << error.message() << '\n';
            return 1;
        }

        smart::global_config().enable_experience = false;
        smart::global_config().enable_experience_persistence = false;
        smart::global_config().enable_prediction_calibration = false;
        smart::global_config().enable_machine_runtime_calibration = true;
        smart::global_config().enable_predictive_decisions = false;
        smart::global_config().enable_predictive_shadow = true;
        smart::global_config().enable_timing_diagnostics = false;

        prepare_pointer_chain(1u << 20);

        const std::filesystem::path candidates_path =
            output_dir / "holdout_candidates.csv";
        const std::filesystem::path summary_path =
            output_dir / "holdout_summary.csv";
        const std::filesystem::path metrics_path =
            output_dir / "holdout_metrics.csv";

        std::ofstream candidates_csv(candidates_path);
        std::ofstream summary_csv(summary_path);
        if (!candidates_csv || !summary_csv)
        {
            std::cerr << "Unable to create holdout CSV files.\n";
            return 1;
        }

        candidates_csv
            << "suite,case,logical_iterations,profile_available,"
            << "profile_median_ms_per_iteration,profile_coefficient_of_variation,"
            << "profile_tail_ratio,profile_parallel_worthiness,profile_regional_cost_ratio,"
            << "hint_available,hint_arithmetic_intensity,hint_branchiness,"
            << "hint_memory_randomness,hint_vectorization_potential,hint_dependency_depth,"
            << "hint_bytes_touched_per_iteration,hint_external_working_set_bytes,"
            << "hint_feature_confidence,plan,jobs,"
            << "predicted_runtime_ms,actual_ms,absolute_error_percent,"
            << "confidence,predicted_execution_ms,scheduling_overhead_ms,"
            << "memory_penalty_ms,imbalance_penalty_ms,"
            << "actual_stddev_ms,actual_ci_low_ms,actual_ci_high_ms,"
            << "analytical_baseline_ms,hierarchical_factor,"
            << "hierarchical_confidence,predicted_runtime_stddev_ms,"
            << "machine_calibration_used,machine_calibration_uncertainty,"
            << "predicted_winner,measured_winner\n";

        summary_csv
            << "suite,case,logical_iterations,predicted_plan,"
            << "measured_best_plan,exact_winner,within_3_percent,"
            << "prediction_confidence,predicted_runtime_ms,"
            << "measured_best_ms,prediction_error_percent,regret_percent\n";

        const std::vector<IntCase> int_cases =
        {
            {"memory", "memory_stream_50k", 50'000,
                memory_stream_work, reset_default},
            {"memory", "memory_stream_500k", 500'000,
                memory_stream_work, reset_default},
            {"memory", "memory_stream_5m", 5'000'000,
                memory_stream_work, reset_default},
            {"cache", "cache_resident_4k", 4'096,
                cache_resident_work, reset_default},
            {"cache", "cache_resident_64k", 65'536,
                cache_resident_work, reset_default},
            {"branch", "branch_heavy_512", 512,
                branch_heavy_work, reset_default},
            {"branch", "branch_heavy_8k", 8'192,
                branch_heavy_work, reset_default},
            {"tiny_heavy", "tiny_heavy_64", 64,
                tiny_heavy_work, reset_default},
            {"tiny_heavy", "tiny_heavy_256", 256,
                tiny_heavy_work, reset_default},
            {"clustered", "clustered_4k", 4'096,
                clustered_work, reset_position},
            {"clustered", "clustered_64k", 65'536,
                clustered_work, reset_position},
            {"sparse", "pointer_chase_4k", 4'096,
                sparse_like_work, reset_default},
            {"sparse", "pointer_chase_64k", 65'536,
                sparse_like_work, reset_default}
        };

        const std::size_t total_cases = int_cases.size() + 4;
        std::vector<ValidationRow> rows;
        rows.reserve(total_cases);
        std::size_t failed = 0;
        std::size_t current = 0;

        auto record = [&](ValidationRow row)
        {
            rows.push_back(row);
            summary_csv
                << row.suite << ','
                << row.case_name << ','
                << row.logical_iterations << ','
                << row.predicted_plan << ','
                << row.measured_best_plan << ','
                << (row.exact_winner ? 1 : 0) << ','
                << (row.within_tolerance ? 1 : 0) << ','
                << row.prediction_confidence << ','
                << row.predicted_runtime_ms << ','
                << row.measured_best_ms << ','
                << row.prediction_error_percent << ','
                << row.regret_percent << '\n';
            candidates_csv.flush();
            summary_csv.flush();

            std::cout
                << "    predicted=" << std::setw(24)
                << row.predicted_plan
                << " | measured=" << std::setw(24)
                << row.measured_best_plan
                << " | exact=" << (row.exact_winner ? "yes" : "no")
                << " | near=" << (row.within_tolerance ? "yes" : "no")
                << " | error=" << std::fixed << std::setprecision(2)
                << row.prediction_error_percent << '%'
                << " | regret=" << row.regret_percent << "%\n";
        };

        for (const IntCase& definition : int_cases)
        {
            ++current;
            std::cout << '[' << current << '/' << total_cases << "] "
                      << definition.name << "...\n";
            try
            {
                record(run_int_case(definition, candidates_csv));
            }
            catch (const std::exception& exception)
            {
                ++failed;
                std::cerr << "    FAILED: " << exception.what() << '\n';
            }
        }

        const std::array<std::pair<const char*, std::size_t>, 2> large_cases =
        {{
            {"large_record_8k", 8'192},
            {"large_record_128k", 131'072}
        }};

        for (const auto& definition : large_cases)
        {
            ++current;
            std::cout << '[' << current << '/' << total_cases << "] "
                      << definition.first << "...\n";
            try
            {
                record(run_large_case(
                    definition.first,
                    definition.second,
                    candidates_csv));
            }
            catch (const std::exception& exception)
            {
                ++failed;
                std::cerr << "    FAILED: " << exception.what() << '\n';
            }
        }

        const std::array<std::tuple<const char*, std::size_t, std::size_t>, 2>
            pair_cases =
        {{
            {"pair_128x128", 128, 128},
            {"pair_512x512", 512, 512}
        }};

        for (const auto& definition : pair_cases)
        {
            ++current;
            std::cout << '[' << current << '/' << total_cases << "] "
                      << std::get<0>(definition) << "...\n";
            try
            {
                record(run_pair_case(
                    std::get<0>(definition),
                    std::get<1>(definition),
                    std::get<2>(definition),
                    candidates_csv));
            }
            catch (const std::exception& exception)
            {
                ++failed;
                std::cerr << "    FAILED: " << exception.what() << '\n';
            }
        }

        if (rows.empty())
        {
            std::cerr << "No holdout cases completed successfully.\n";
            return 2;
        }

        std::size_t exact = 0;
        std::size_t near_count = 0;
        std::vector<double> errors;
        std::vector<double> regrets;
        for (const ValidationRow& row : rows)
        {
            exact += row.exact_winner ? 1u : 0u;
            near_count += row.within_tolerance ? 1u : 0u;
            errors.push_back(row.prediction_error_percent);
            regrets.push_back(row.regret_percent);
        }

        const double exact_accuracy =
            static_cast<double>(exact) / rows.size() * 100.0;
        const double near_accuracy =
            static_cast<double>(near_count) / rows.size() * 100.0;
        const double mean_error =
            std::accumulate(errors.begin(), errors.end(), 0.0) / errors.size();
        const double median_error = percentile(errors, 0.50);
        const double p90_error = percentile(errors, 0.90);
        const double mean_regret =
            std::accumulate(regrets.begin(), regrets.end(), 0.0) / regrets.size();
        const double median_regret = percentile(regrets, 0.50);
        const double p90_regret = percentile(regrets, 0.90);
        const double worst_regret = *std::max_element(
            regrets.begin(), regrets.end());

        std::ofstream metrics_csv(metrics_path);
        metrics_csv << "metric,value\n";
        metrics_csv << "cases," << rows.size() << '\n';
        metrics_csv << "failed_cases," << failed << '\n';
        metrics_csv << "exact_winner_accuracy_percent,"
                    << exact_accuracy << '\n';
        metrics_csv << "within_3_percent_accuracy_percent,"
                    << near_accuracy << '\n';
        metrics_csv << "mean_prediction_error_percent,"
                    << mean_error << '\n';
        metrics_csv << "median_prediction_error_percent,"
                    << median_error << '\n';
        metrics_csv << "p90_prediction_error_percent,"
                    << p90_error << '\n';
        metrics_csv << "mean_regret_percent," << mean_regret << '\n';
        metrics_csv << "median_regret_percent," << median_regret << '\n';
        metrics_csv << "p90_regret_percent," << p90_regret << '\n';
        metrics_csv << "worst_regret_percent," << worst_regret << '\n';
        const auto regret_summary =
            smart::validation::regret_metrics(regrets);
        metrics_csv << "catastrophic_choices_over_10_percent,"
                    << regret_summary.catastrophic_10_percent << '\n';
        metrics_csv << "catastrophic_choices_over_25_percent,"
                    << regret_summary.catastrophic_25_percent << '\n';

        std::cout << "\nHoldout summary\n";
        std::cout << "Successful cases       : " << rows.size() << '\n';
        std::cout << "Failed cases           : " << failed << '\n';
        std::cout << "Exact winner accuracy  : "
                  << exact_accuracy << "%\n";
        std::cout << "Within 3% accuracy     : "
                  << near_accuracy << "%\n";
        std::cout << "Median prediction error: "
                  << median_error << "%\n";
        std::cout << "Median regret          : "
                  << median_regret << "%\n";
        std::cout << "Worst regret           : "
                  << worst_regret << "%\n";
        std::cout << "\nResults written to:\n"
                  << output_dir.string() << '\n';

        return failed == 0 ? 0 : 2;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Holdout validation failed: "
                  << exception.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "Holdout validation failed with an unknown error.\n";
        return 1;
    }
}
