#pragma once

#include <cstddef>
#include <smart/decision/execution_plan.hpp>
#include <smart/workload/fingerprint.hpp>

namespace smart
{
struct ExperienceEntry
{
    WorkloadFingerprint fingerprint;

    ExecutionEngineType engine = ExecutionEngineType::ThreadPool;
    ExecutionStrategy strategy = ExecutionStrategy::StaticChunks;

    double best_elapsed_ms = 0.0;
    double average_elapsed_ms = 0.0;
    double last_elapsed_ms = 0.0;

    double variance_ms = 0.0;
    double standard_deviation_ms = 0.0;

    std::size_t job_count = 1;

    std::size_t chunk_size = 0;
    std::size_t sample_count = 0;

    // Prediction feedback for this exact fingerprint and plan.
    double last_predicted_ms = 0.0;
    double last_prediction_error_percent = 0.0;
    double average_absolute_prediction_error_percent = 0.0;
    double average_runtime_correction = 1.0;
    std::size_t prediction_sample_count = 0;

    // Outcome-aware ranking state. Effective weight and EWMA values
    // decay over time so stale observations cannot dominate forever.
    double effective_sample_weight = 0.0;
    double decayed_elapsed_ms = 0.0;
    double decayed_regret_percent = 0.0;
    double decayed_success_rate = 0.5;
    double last_regret_percent = 0.0;
    std::size_t outcome_sample_count = 0;

    double confidence = 0.0;
    bool valid = false;
};
} // namespace smart
