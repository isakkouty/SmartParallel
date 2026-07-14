#include <smart/core/config.hpp>
#include <smart/decision/predictive_decision_model.hpp>
#include <smart/execution/executor.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/profiling/isolated_function_profile.hpp>
#include <smart/workload/fingerprint.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_builder.hpp>

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
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    volatile std::uint64_t ranking_sink = 0;

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

        return std::string(smart::engine_name(plan.engine)) + "/" +
            strategy_name(plan.strategy) + "/w" +
            std::to_string(plan.job_count) + "/c" +
            std::to_string(plan.chunk_size);
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

        return left.parallel == right.parallel &&
            left.engine == right.engine &&
            left.strategy == right.strategy &&
            left.job_count == right.job_count &&
            left.chunk_size == right.chunk_size;
    }

    smart::FunctionProfiler::Config profile_config()
    {
        smart::FunctionProfiler::Config config;
        config.min_samples = 6;
        config.max_samples = 20;
        config.batch_size = 1;
        config.max_batch_size = 256;
        config.max_callback_invocations = 4096;
        config.max_profile_time_ms = 20.0;
        config.target_batch_duration_ms = 0.02;
        return config;
    }

    using WorkFunction = void (*)(int&);
    using ResetFunction = void (*)(std::vector<int>&);

    struct LearningCase
    {
        const char* name;
        std::size_t size;
        WorkFunction work;
        ResetFunction reset;
    };

    void reset_default(std::vector<int>& values)
    {
        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = static_cast<int>((i * 17u + 3u) % 4096u);
    }

    void reset_position(std::vector<int>& values)
    {
        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = static_cast<int>(i);
    }

    void cheap_work(int& value)
    {
        value = value * 33 + 7;
    }

    void compute_work(int& value)
    {
        std::uint32_t x = static_cast<std::uint32_t>(value + 1);
        for (int i = 0; i < 320; ++i)
            x = x * 1664525u + 1013904223u + static_cast<std::uint32_t>(i);
        value = static_cast<int>(x);
    }

    void irregular_work(int& value)
    {
        const int loops = 40 + (std::abs(value) % 1200);
        std::uint32_t x = static_cast<std::uint32_t>(value + 1);
        for (int i = 0; i < loops; ++i)
            x = (x * 22695477u + 1u) ^ (x >> 7);
        value = static_cast<int>(x);
    }

    void clustered_work(int& value)
    {
        const std::size_t position = static_cast<std::size_t>(value);
        const int loops = (position % 4096u) >= 3072u ? 1600 : 80;
        std::uint32_t x = static_cast<std::uint32_t>(value + 1);
        for (int i = 0; i < loops; ++i)
            x = x * 1103515245u + 12345u;
        value = static_cast<int>(x);
    }

    void consume(const std::vector<int>& values)
    {
        std::uint64_t sum = 0;
        const std::size_t stride = std::max<std::size_t>(1, values.size() / 256);
        for (std::size_t i = 0; i < values.size(); i += stride)
            sum = sum * 131u + static_cast<std::uint32_t>(values[i]);
        ranking_sink += sum;
    }

    double measure_plan_once(
        const smart::Workload& workload,
        const smart::ExecutionPlan& plan,
        std::vector<int>& values,
        WorkFunction work,
        ResetFunction reset)
    {
        reset(values);
        const double start = now_ms();
        smart::execute_workload(workload, plan, [&](std::size_t index)
        {
            work(values[index]);
        });
        const double elapsed = now_ms() - start;
        consume(values);
        return elapsed;
    }

    struct RoundResult
    {
        std::size_t round = 0;
        smart::ExecutionPlan recommended;
        smart::ExecutionPlan measured_best;
        bool exact = false;
        bool near_best = false;
        double regret_percent = 0.0;
        double recommended_actual_ms = 0.0;
        double best_actual_ms = 0.0;
        double history_weight = 0.0;
        std::size_t history_samples = 0;
        bool experience_used = false;
    };

    RoundResult run_learning_round(
        const LearningCase& definition,
        std::size_t round,
        const smart::Workload& workload,
        const smart::WorkloadAnalysis& analysis,
        const smart::FunctionProfile& profile,
        std::vector<int>& values,
        std::ofstream& candidate_csv)
    {
        const smart::PredictiveDecisionResult prediction =
            smart::PredictiveDecisionModel().predict(
                workload,
                analysis,
                &profile);

        if (!prediction.available || prediction.candidates.empty())
            throw std::runtime_error("Prediction unavailable");

        const smart::WorkloadFingerprint fp =
            smart::fingerprint(workload, &profile);

        double best_actual = std::numeric_limits<double>::max();
        double recommended_actual = 0.0;
        smart::ExecutionPlan best_plan;
        bool recommended_found = false;
        double recommendation_history_weight = 0.0;
        std::size_t recommendation_history_samples = 0;
        bool recommendation_experience_used = false;

        struct MeasuredCandidate
        {
            smart::PlanCostEstimate estimate;
            double actual_ms = 0.0;
        };
        std::vector<MeasuredCandidate> measured_candidates;

        for (const smart::PlanCostEstimate& candidate : prediction.candidates)
        {
            if (!candidate.available)
                continue;

            const double actual = measure_plan_once(
                workload,
                candidate.plan,
                values,
                definition.work,
                definition.reset);

            measured_candidates.push_back({candidate, actual});

            if (actual < best_actual)
            {
                best_actual = actual;
                best_plan = candidate.plan;
            }

            const bool recommended = same_plan(
                candidate.plan,
                prediction.recommended_plan);
            if (recommended)
            {
                recommended_found = true;
                recommended_actual = actual;
                recommendation_history_weight =
                    candidate.ranking_history_weight;
                recommendation_history_samples =
                    candidate.ranking_samples;
                recommendation_experience_used =
                    candidate.experience_rank_used;
            }

            candidate_csv
                << definition.name << ','
                << round << ','
                << plan_name(candidate.plan) << ','
                << candidate.predicted_total_ms << ','
                << candidate.analytical_rank_score << ','
                << candidate.historical_rank_score << ','
                << candidate.ranking_score << ','
                << candidate.ranking_history_weight << ','
                << candidate.ranking_samples << ','
                << candidate.ranking_regret_percent << ','
                << candidate.ranking_success_rate << ','
                << candidate.ranking_uncertainty << ','
                << candidate.ranking_similarity << ','
                << (candidate.similarity_rank_used ? 1 : 0) << ','
                << (candidate.experience_rank_used ? 1 : 0) << ','
                << actual << ','
                << (recommended ? 1 : 0)
                << '\n';
        }

        if (!recommended_found || !std::isfinite(best_actual))
            throw std::runtime_error("Unable to match measured candidates");

        for (const MeasuredCandidate& measured : measured_candidates)
        {
            smart::global_experience_database().record_outcome(
                fp,
                measured.estimate.plan,
                measured.actual_ms,
                best_actual,
                measured.estimate.predicted_total_ms);
        }

        RoundResult result;
        result.round = round;
        result.recommended = prediction.recommended_plan;
        result.measured_best = best_plan;
        result.exact = same_plan(result.recommended, result.measured_best);
        result.recommended_actual_ms = recommended_actual;
        result.best_actual_ms = best_actual;
        result.regret_percent = best_actual > 0.0
            ? (recommended_actual - best_actual) / best_actual * 100.0
            : 0.0;
        result.near_best = result.regret_percent <= 3.0;
        result.history_weight = recommendation_history_weight;
        result.history_samples = recommendation_history_samples;
        result.experience_used = recommendation_experience_used;
        return result;
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
            throw std::runtime_error("Unable to determine working directory");

        const std::filesystem::path output_dir =
            root / "validation" / "output";
        std::filesystem::create_directories(output_dir, error);
        if (error)
            throw std::runtime_error("Unable to create output directory");

        const std::filesystem::path candidate_path =
            output_dir / "ranking_evolution_candidates.csv";
        const std::filesystem::path summary_path =
            output_dir / "ranking_evolution_summary.csv";
        const std::filesystem::path metrics_path =
            output_dir / "ranking_evolution_metrics.csv";
        const std::filesystem::path database_path =
            output_dir / "ranking_evolution_experience.db";

        std::ofstream candidate_csv(candidate_path);
        std::ofstream summary_csv(summary_path);
        if (!candidate_csv || !summary_csv)
            throw std::runtime_error("Unable to create ranking CSV files");

        candidate_csv
            << "case,round,plan,predicted_total_ms,analytical_rank_score,"
            << "historical_rank_score,ranking_score,history_weight,"
            << "history_samples,ranking_regret_percent,ranking_success_rate,"
            << "ranking_uncertainty,ranking_similarity,similarity_used,"
            << "experience_used,actual_ms,recommended\n";
        summary_csv
            << "case,round,recommended_plan,measured_best_plan,exact_winner,"
            << "within_3_percent,regret_percent,recommended_actual_ms,"
            << "best_actual_ms,history_weight,history_samples,experience_used\n";

        smart::Config& config = smart::global_config();
        config.enable_experience = true;
        config.enable_experience_persistence = false;
        config.enable_prediction_calibration = false;
        config.enable_experience_ranking = true;
        config.minimum_ranking_samples = 3;
        config.maximum_ranking_history_weight = 0.90;
        config.enable_predictive_decisions = false;
        config.enable_predictive_shadow = true;
        config.enable_machine_runtime_calibration = true;
        config.enable_adaptive_execution_candidates = true;

        const std::vector<LearningCase> cases =
        {
            {"cheap_100k", 100'000, cheap_work, reset_default},
            {"compute_10k", 10'000, compute_work, reset_default},
            {"irregular_10k", 10'000, irregular_work, reset_default},
            {"clustered_16k", 16'384, clustered_work, reset_position}
        };

        constexpr std::size_t training_rounds = 7;
        std::size_t total_rounds = 0;
        std::size_t exact_rounds = 0;
        std::size_t near_rounds = 0;
        double total_regret = 0.0;
        double cold_regret = 0.0;
        double final_regret = 0.0;

        std::cout << "==== SmartParallel Ranking Evolution Validation ====\n";

        for (const LearningCase& definition : cases)
        {
            smart::global_experience_database().clear();
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

            if (!profile.available)
                throw std::runtime_error("Function profiling unavailable");

            std::cout << "\n" << definition.name << '\n';

            for (std::size_t round = 0; round < training_rounds; ++round)
            {
                const RoundResult result = run_learning_round(
                    definition,
                    round,
                    workload,
                    analysis,
                    profile,
                    values,
                    candidate_csv);

                summary_csv
                    << definition.name << ','
                    << round << ','
                    << plan_name(result.recommended) << ','
                    << plan_name(result.measured_best) << ','
                    << (result.exact ? 1 : 0) << ','
                    << (result.near_best ? 1 : 0) << ','
                    << result.regret_percent << ','
                    << result.recommended_actual_ms << ','
                    << result.best_actual_ms << ','
                    << result.history_weight << ','
                    << result.history_samples << ','
                    << (result.experience_used ? 1 : 0)
                    << '\n';

                ++total_rounds;
                exact_rounds += result.exact ? 1u : 0u;
                near_rounds += result.near_best ? 1u : 0u;
                total_regret += result.regret_percent;
                if (round == 0)
                    cold_regret += result.regret_percent;
                if (round + 1 == training_rounds)
                    final_regret += result.regret_percent;

                std::cout
                    << "  round " << round
                    << " | recommended=" << std::setw(36)
                    << plan_name(result.recommended)
                    << " | best=" << std::setw(36)
                    << plan_name(result.measured_best)
                    << " | regret=" << std::fixed << std::setprecision(2)
                    << result.regret_percent << '%'
                    << " | history="
                    << (result.experience_used ? "yes" : "no")
                    << " w=" << result.history_weight
                    << " n=" << result.history_samples
                    << '\n';
            }

            if (!smart::global_experience_database().save_to_file(
                    database_path.string()))
            {
                throw std::runtime_error("Unable to save experience database");
            }

            smart::global_experience_database().clear();
            if (!smart::global_experience_database().load_from_file(
                    database_path.string()))
            {
                throw std::runtime_error("Unable to reload experience database");
            }

            const smart::PredictiveDecisionResult after_reload =
                smart::PredictiveDecisionModel().predict(
                    workload,
                    analysis,
                    &profile);
            if (!after_reload.available)
                throw std::runtime_error("Prediction unavailable after reload");

            std::cout
                << "  reload recommendation: "
                << plan_name(after_reload.recommended_plan)
                << '\n';
        }

        const double case_count = static_cast<double>(cases.size());
        const double cold_mean_regret = cold_regret / case_count;
        const double final_mean_regret = final_regret / case_count;
        const double improvement = cold_mean_regret - final_mean_regret;

        std::ofstream metrics_csv(metrics_path);
        if (!metrics_csv)
            throw std::runtime_error("Unable to create ranking metrics file");

        metrics_csv << "metric,value\n";
        metrics_csv << "cases," << cases.size() << '\n';
        metrics_csv << "training_rounds," << training_rounds << '\n';
        metrics_csv << "exact_winner_accuracy_percent,"
                    << static_cast<double>(exact_rounds) /
                        static_cast<double>(total_rounds) * 100.0 << '\n';
        metrics_csv << "within_3_percent_accuracy_percent,"
                    << static_cast<double>(near_rounds) /
                        static_cast<double>(total_rounds) * 100.0 << '\n';
        metrics_csv << "mean_regret_percent,"
                    << total_regret / static_cast<double>(total_rounds) << '\n';
        metrics_csv << "cold_start_mean_regret_percent,"
                    << cold_mean_regret << '\n';
        metrics_csv << "final_round_mean_regret_percent,"
                    << final_mean_regret << '\n';
        metrics_csv << "regret_improvement_points,"
                    << improvement << '\n';

        std::cout << "\nRanking evolution summary\n";
        std::cout << "Cold-start mean regret : "
                  << cold_mean_regret << "%\n";
        std::cout << "Final-round mean regret: "
                  << final_mean_regret << "%\n";
        std::cout << "Improvement             : "
                  << improvement << " percentage points\n";
        std::cout << "Results written to:\n"
                  << output_dir.string() << '\n';

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Ranking evolution validation failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
