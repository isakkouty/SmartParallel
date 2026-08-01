#pragma once

#include <iostream>
#include <smart/decision/decision_report.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/workload/observation.hpp>

namespace smart
{
inline const char* profile_stop_reason_name(ProfileStopReason reason)
{
    switch (reason)
    {
        case ProfileStopReason::None:
            return "None";
        case ProfileStopReason::ConfidenceReached:
            return "ConfidenceReached";
        case ProfileStopReason::TimeBudgetReached:
            return "TimeBudgetReached";
        case ProfileStopReason::InvocationBudgetReached:
            return "InvocationBudgetReached";
        case ProfileStopReason::MaximumSamplesReached:
            return "MaximumSamplesReached";
        case ProfileStopReason::WorkloadExhausted:
            return "WorkloadExhausted";
        case ProfileStopReason::MeasurementUnreliable:
            return "MeasurementUnreliable";
    }

    return "Unknown";
}

inline const char* storage_kind_name(StorageKind kind)
{
    switch (kind)
    {
        case StorageKind::Contiguous:
            return "Contiguous";
        case StorageKind::Segmented:
            return "Segmented";
        case StorageKind::NodeBased:
            return "NodeBased";
        case StorageKind::ProxyReference:
            return "ProxyReference";
        case StorageKind::IndexGenerated:
            return "IndexGenerated";
        case StorageKind::Unknown:
            return "Unknown";
    }

    return "Unknown";
}

inline const char* execution_strategy_name(ExecutionStrategy strategy)
{
    switch (strategy)
    {
        case ExecutionStrategy::Sequential:
            return "Sequential";
        case ExecutionStrategy::StaticChunks:
            return "StaticChunks";
        case ExecutionStrategy::DynamicChunks:
            return "DynamicChunks";
    }

    return "Unknown";
}

inline void print_decision_report(const DecisionReport& report, std::ostream& out = std::cout)
{
    const StructuralObservations& structure = report.analysis.structural;

    out << "==== SmartParallel Decision Report ====\n";

    out << "\n[Structure]\n";
    out << "Observation source: " << observation_source_name(structure.metadata.source) << "\n";
    out << "Observation confidence: " << observation_confidence_name(structure.metadata.confidence)
        << "\n";
    out << "Logical iterations: " << structure.logical_iterations << "\n";
    out << "Dimensions: " << structure.dimensionality << "\n";
    out << "Represented input bytes: " << structure.represented_input_bytes << "\n";
    out << "Unique input elements: " << structure.unique_input_elements << "\n";

    for (std::size_t index = 0; index < structure.dimensions.size(); ++index)
    {
        const DimensionAnalysis& dimension = structure.dimensions[index];

        out << "Dimension " << index << ": extent=" << dimension.extent
            << ", bytes=" << dimension.representation_bytes << ", reuse=" << dimension.reuse_factor
            << ", storage=" << storage_kind_name(dimension.storage_kind) << ", contiguous=";

        if (dimension.contiguous_known)
        {
            out << (dimension.contiguous ? "yes" : "no");
        }
        else
        {
            out << "unknown";
        }

        out << ", random_access=";
        if (dimension.random_access_known)
        {
            out << (dimension.random_access ? "yes" : "no");
        }
        else
        {
            out << "unknown";
        }

        out << "\n";
    }

    out << "\n[Hardware-relative scale]\n";
    out << "Iterations per logical thread: " << structure.iterations_per_logical_thread << "\n";
    out << "Iterations per physical core: " << structure.iterations_per_physical_core << "\n";

    if (structure.cache_ratios_available)
    {
        out << "L1 residency ratio: " << structure.l1_residency_ratio << "\n";
        out << "L2 residency ratio: " << structure.l2_residency_ratio << "\n";
        out << "L3 residency ratio: " << structure.l3_residency_ratio << "\n";
    }
    else
    {
        out << "Cache residency ratios: unavailable\n";
    }

    out << "Performance model uses sensor cache ratios: "
        << report.model.used_structural_cache_observations << "\n";
    out << "Model L1 pressure: " << report.model.l1_pressure << "\n";
    out << "Model L2 pressure: " << report.model.l2_pressure << "\n";
    out << "Model L3 pressure: " << report.model.l3_pressure << "\n";
    out << "Page pressure: " << report.model.page_pressure << "\n";
    out << "Likely memory sensitive: " << report.model.likely_memory_sensitive << "\n";

    out << "\n[Function profile]\n";
    if (report.has_function_profile)
    {
        const FunctionProfile& profile = report.function_profile;

        out << "Available: " << profile.available << "\n";
        out << "Observation source: " << observation_source_name(profile.metadata.source) << "\n";
        out << "Observation confidence: "
            << observation_confidence_name(profile.metadata.confidence) << "\n";
        out << "Stop reason: " << profile_stop_reason_name(profile.stop_reason) << "\n";
        out << "Measured batches: " << profile.measured_batches << "\n";
        out << "Callback invocations: " << profile.callback_invocations << "\n";
        out << "Chosen batch size: " << profile.chosen_batch_size << "\n";
        out << "Profiling elapsed ms: " << profile.profiling_elapsed_ms << "\n";
        out << "Measurement reliable: " << profile.measurement_reliable << "\n";
        out << "Signal/floor ratio: " << profile.signal_to_floor_ratio << "\n";
        out << "Trimmed mean ms/iteration: " << profile.trimmed_mean_ms_per_iteration << "\n";
        out << "Median ms/iteration: " << profile.median_ms_per_iteration << "\n";
        out << "Coefficient of variation: " << profile.coefficient_of_variation << "\n";
        out << "Tail ratio: " << profile.tail_ratio << "\n";
        out << "Warm-up detected: " << profile.warmup_detected << "\n";
        out << "Warm-up ratio: " << profile.warmup_ratio << "\n";
        out << "Estimated setup cost ms: " << profile.estimated_setup_cost_ms << "\n";
        out << "Local median ms/iteration: " << profile.local_median_ms_per_iteration << "\n";
        out << "Distributed median ms/iteration: " << profile.distributed_median_ms_per_iteration
            << "\n";
        out << "Distributed/local ratio: " << profile.distributed_to_local_ratio << "\n";
        out << "Regional cost ratio: " << profile.regional_cost_ratio << "\n";
    }
    else
    {
        out << "Unavailable\n";
    }

    out << "\n[Predictive model]\n";
    out << "Available: " << report.predictive_model_available << "\n";
    out << "Mode: " << (report.predictive_shadow_mode ? "Shadow" : "Control") << "\n";

    if (report.predictive_model_available)
    {
        for (std::size_t index = 0; index < report.predictive_candidates.size(); ++index)
        {
            const PlanCostEstimate& candidate = report.predictive_candidates[index];

            out << "Candidate " << index << ": engine=" << engine_name(candidate.plan.engine)
                << ", strategy=" << execution_strategy_name(candidate.plan.strategy)
                << ", jobs=" << candidate.plan.job_count << ", chunk=" << candidate.plan.chunk_size
                << ", useful_ms=" << candidate.useful_work_ms
                << ", execution_ms=" << candidate.predicted_execution_ms
                << ", scheduling_ms=" << candidate.scheduling_overhead_ms
                << ", memory_penalty_ms=" << candidate.memory_penalty_ms
                << ", imbalance_penalty_ms=" << candidate.imbalance_penalty_ms
                << ", framework_ms=" << candidate.framework_overhead_ms
                << ", total_ms=" << candidate.predicted_total_ms
                << ", analytical_baseline_ms=" << candidate.analytical_baseline_total_ms
                << ", hierarchical_factor=" << candidate.hierarchical_residual_factor
                << ", hierarchical_confidence=" << candidate.hierarchical_residual_confidence
                << ", runtime_stddev_ms=" << candidate.predicted_runtime_stddev_ms
                << ", risk_adjusted_ms=" << candidate.risk_adjusted_total_ms
                << ", override_candidate=" << candidate.learned_override_candidate
                << ", override_allowed=" << candidate.learned_override_allowed
                << ", efficiency=" << candidate.predicted_parallel_efficiency
                << ", speedup=" << candidate.predicted_speedup
                << ", confidence=" << candidate.confidence
                << ", calibrated=" << candidate.calibration_applied
                << ", calibration_factor=" << candidate.calibration_factor
                << ", calibration_samples=" << candidate.calibration_samples << "\n";
        }

        out << "Recommended engine: " << engine_name(report.predictive_plan.engine) << "\n";
        out << "Recommended strategy: " << execution_strategy_name(report.predictive_plan.strategy)
            << "\n";
        out << "Recommended jobs: " << report.predictive_plan.job_count << "\n";
        out << "Recommended chunk size: " << report.predictive_plan.chunk_size << "\n";
        out << "Recommended total ms: " << report.predictive_total_ms << "\n";
        out << "Prediction confidence: " << report.predictive_confidence << "\n";
        out << "Matches selected plan: " << report.predictive_plan_matches_selected << "\n";
        out << "Applied to execution: " << report.predictive_plan_applied << "\n";
    }

    out << "\n[Experience and calibration]\n";
    out << "Execution recorded: " << report.experience_recorded << "\n";
    out << "Persistence enabled: " << report.experience_persistence_enabled << "\n";
    out << "Autosave performed: " << report.experience_saved << "\n";
    out << "Actual selected-plan execution ms: " << report.actual_execution_ms << "\n";
    out << "Predicted selected-plan execution ms: " << report.selected_plan_predicted_ms << "\n";
    out << "Prediction error percent: " << report.prediction_error_percent << "\n";
    out << "Plan experience samples: " << report.experience_samples << "\n";
    out << "Prediction feedback samples: " << report.prediction_experience_samples << "\n";
    out << "Learned runtime correction: " << report.learned_runtime_correction << "\n";

    out << "\n[Decision]\n";
    out << "Scheduling preference: ";

    switch (report.execution.scheduling)
    {
        case SchedulingPreference::Sequential:
            out << "Sequential";
            break;
        case SchedulingPreference::Static:
            out << "Static";
            break;
        case SchedulingPreference::Dynamic:
            out << "Dynamic";
            break;
        default:
            out << "Unknown";
            break;
    }

    out << "\n";
    out << "Memory locality critical: " << report.execution.memory_locality_critical << "\n";
    out << "Scheduler overhead sensitive: " << report.execution.scheduler_overhead_sensitive
        << "\n";
    out << "Load balancing important: " << report.execution.load_balancing_important << "\n";
    out << "ThreadPool score: " << report.thread_pool_score << "\n";
    out << "StaticThread score: " << report.static_thread_score << "\n";
    out << "OneTbb score: " << report.one_tbb_score << "\n";
    out << "Selected engine: " << engine_name(report.plan.engine) << "\n";
    out << "Decision source: ";
    switch (report.source)
    {
        case DecisionSource::Analytical:
            out << "Analytical";
            break;
        case DecisionSource::Historical:
            out << "Historical";
            break;
        case DecisionSource::Predictive:
            out << "Predictive";
            break;
    }
    out << "\n";
    out << "Decision confidence: " << report.decision_confidence << "\n";
}
} // namespace smart
