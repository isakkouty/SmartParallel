#pragma once

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <mutex>
#include <string>

#include <smart/core/config.hpp>
#include <smart/decision/decision_report.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/profiling/function_profiler.hpp>
#include <smart/workload/fingerprint.hpp>
#include <smart/workload/workload.hpp>
#include <smart/decision/exploration_policy.hpp>
#include <smart/decision/hierarchical_residual_model.hpp>

namespace smart
{
    namespace detail
    {
        struct ResidualLoadState
        {
            std::mutex mutex;
            std::string loaded_path;
        };

        inline ResidualLoadState& residual_load_state()
        {
            static ResidualLoadState state;
            return state;
        }

        inline void mark_residual_loaded(const std::string& path)
        {
            ResidualLoadState& state = residual_load_state();
            std::lock_guard<std::mutex> lock(state.mutex);
            state.loaded_path = path;
        }
    }

    inline void ensure_experience_loaded()
    {
        const Config& config = global_config();
        if (!config.enable_experience ||
            !config.enable_experience_persistence)
        {
            return;
        }

        global_experience_database().load_once(
            config.experience_file_path);

        const std::string residual_path =
            config.experience_file_path + ".residual";
        detail::ResidualLoadState& residual_state =
            detail::residual_load_state();
        std::lock_guard<std::mutex> residual_lock(residual_state.mutex);
        if (residual_state.loaded_path == residual_path)
            return;

        std::ifstream residual_probe(residual_path);
        if (residual_probe.good())
        {
            residual_probe.close();
            global_hierarchical_residual_learner().load_from_file(
                residual_path);
        }
        // Mark even a missing file as checked. Otherwise every execution would
        // retry the load and a newly learned in-memory model could later be
        // overwritten by a stale file appearing at the same path.
        residual_state.loaded_path = residual_path;
    }

    inline const PlanCostEstimate* predictive_candidate_for_plan(
        const DecisionReport& report,
        const ExecutionPlan& plan)
    {
        for (const PlanCostEstimate& candidate : report.predictive_candidates)
        {
            if (!candidate.available)
                continue;
            if (candidate.plan.engine == plan.engine &&
                candidate.plan.strategy == plan.strategy &&
                candidate.plan.parallel == plan.parallel &&
                candidate.plan.job_count == plan.job_count &&
                candidate.plan.chunk_size == plan.chunk_size)
            {
                return &candidate;
            }
        }
        return nullptr;
    }

    inline double predicted_runtime_for_plan(
        const DecisionReport& report,
        const ExecutionPlan& plan)
    {
        const PlanCostEstimate* candidate =
            predictive_candidate_for_plan(report, plan);
        if (candidate == nullptr)
            return 0.0;

        // ExecutionStats measures the execution section, not the
        // profiling/analysis framework phases.
        return std::max(
            0.0,
            candidate->predicted_total_ms -
                candidate->framework_overhead_ms);
    }

    inline void record_execution_experience(
        const Workload& workload,
        const FunctionProfile* profile,
        DecisionReport& report,
        const ExecutionPlan& plan,
        double elapsed_ms)
    {
        const Config& config = global_config();
        if (!config.enable_experience)
            return;

        ensure_experience_loaded();

        const WorkloadFingerprint fp = fingerprint(workload, profile);
        const double predicted_ms =
            predicted_runtime_for_plan(report, plan);

        ExperienceDatabase& database = global_experience_database();
        const PlanCostEstimate* selected_candidate =
            predictive_candidate_for_plan(report, plan);
        if (selected_candidate != nullptr &&
            config.enable_hierarchical_residual_learning)
        {
            global_hierarchical_residual_learner().record(
                *selected_candidate,
                elapsed_ms);
        }
        database.record(fp, plan, elapsed_ms, predicted_ms);

        global_online_exploration_policy().record_result(
            fp,
            report.exploration_applied,
            elapsed_ms,
            report.exploitation_expected_ms);
        report.exploration_cooldown_remaining =
            global_online_exploration_policy().cooldown_remaining(fp);

        report.experience_recorded = true;
        report.experience_persistence_enabled =
            config.enable_experience_persistence;
        report.actual_execution_ms = elapsed_ms;
        report.selected_plan_predicted_ms = predicted_ms;
        report.prediction_error_percent =
            predicted_ms > 0.0
                ? ((elapsed_ms - predicted_ms) / predicted_ms) * 100.0
                : 0.0;

        const ExperienceEntry* entry = database.find_plan(fp, plan);
        if (entry != nullptr)
        {
            report.experience_samples = entry->sample_count;
            report.prediction_experience_samples =
                entry->prediction_sample_count;
            report.learned_runtime_correction =
                entry->average_runtime_correction;
        }

        if (config.enable_experience_persistence &&
            config.enable_experience_autosave)
        {
            const std::size_t dirty_before = database.dirty_records();
            const bool saved = database.save_if_due(
                config.experience_file_path,
                config.experience_autosave_interval);
            report.experience_saved =
                saved && dirty_before > 0 && database.dirty_records() == 0;
            if (report.experience_saved &&
                config.enable_hierarchical_residual_learning)
            {
                global_hierarchical_residual_learner().save_to_file(
                    config.experience_file_path + ".residual");
            }
        }
    }

    inline bool flush_experience()
    {
        const Config& config = global_config();
        if (!config.enable_experience_persistence)
            return false;

        const bool database_saved =
            global_experience_database().save_to_file(
                config.experience_file_path);
        const bool residual_saved =
            !config.enable_hierarchical_residual_learning ||
            global_hierarchical_residual_learner().save_to_file(
                config.experience_file_path + ".residual");
        return database_saved && residual_saved;
    }

    inline bool load_experience(const std::string& path)
    {
        const bool database_loaded =
            global_experience_database().load_from_file(path);
        std::ifstream residual_probe(path + ".residual");
        if (!residual_probe.good())
            return database_loaded;
        residual_probe.close();
        const bool residual_loaded =
            global_hierarchical_residual_learner().load_from_file(
                path + ".residual");
        if (residual_loaded)
            detail::mark_residual_loaded(path + ".residual");
        return database_loaded && residual_loaded;
    }

    inline bool save_experience(const std::string& path)
    {
        const bool database_saved =
            global_experience_database().save_to_file(path);
        const bool residual_saved =
            !global_config().enable_hierarchical_residual_learning ||
            global_hierarchical_residual_learner().save_to_file(
                path + ".residual");
        return database_saved && residual_saved;
    }
}
