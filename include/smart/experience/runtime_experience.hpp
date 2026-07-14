#pragma once

#include <algorithm>
#include <cstddef>

#include <smart/core/config.hpp>
#include <smart/decision/decision_report.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/profiling/function_profiler.hpp>
#include <smart/workload/fingerprint.hpp>
#include <smart/workload/workload.hpp>
#include <smart/decision/exploration_policy.hpp>

namespace smart
{
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
    }

    inline double predicted_runtime_for_plan(
        const DecisionReport& report,
        const ExecutionPlan& plan)
    {
        for (const PlanCostEstimate& candidate :
             report.predictive_candidates)
        {
            if (!candidate.available)
                continue;

            if (candidate.plan.engine == plan.engine &&
                candidate.plan.strategy == plan.strategy &&
                candidate.plan.parallel == plan.parallel &&
                candidate.plan.job_count == plan.job_count &&
                candidate.plan.chunk_size == plan.chunk_size)
            {
                // ExecutionStats measures the execution section, not the
                // profiling/analysis framework phases.
                return std::max(
                    0.0,
                    candidate.predicted_total_ms -
                        candidate.framework_overhead_ms);
            }
        }

        return 0.0;
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
        }
    }

    inline bool flush_experience()
    {
        const Config& config = global_config();
        if (!config.enable_experience_persistence)
            return false;

        return global_experience_database().save_to_file(
            config.experience_file_path);
    }

    inline bool load_experience(const std::string& path)
    {
        return global_experience_database().load_from_file(path);
    }

    inline bool save_experience(const std::string& path)
    {
        return global_experience_database().save_to_file(path);
    }
}
