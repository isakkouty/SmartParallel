#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <smart/decision/plan_prediction.hpp>
#include <smart/decision/candidate_ranker.hpp>
#include <smart/decision/confidence_model.hpp>
#include <smart/decision/runtime_calibration.hpp>
#include <smart/decision/family_calibration.hpp>
#include <smart/decision/memory_bandwidth_calibration.hpp>
#include <smart/decision/memory_access_calibration.hpp>
#include <smart/decision/profile_cost_calibration.hpp>
#include <smart/decision/runtime_scaling_model.hpp>
#include <smart/decision/residual_correction.hpp>
#include <smart/core/config.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/hardware/hardware_characteristics.hpp>
#include <smart/workload/fingerprint.hpp>
#include <smart/profiling/function_profiler.hpp>
#include <smart/workload/workload.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/model/performance_model.hpp>
#include <smart/model/hardware_prediction.hpp>
#include <smart/workload/workload_family.hpp>

namespace smart
{
    class PredictiveDecisionModel
    {
    public:
        PredictiveDecisionResult predict(
            const Workload& workload,
            const WorkloadAnalysis& analysis,
            const FunctionProfile* profile) const
        {
            PredictiveDecisionResult result;

            if (profile == nullptr ||
                !profile->available ||
                workload.iterations == 0)
            {
                return result;
            }

            const double linear_useful_work_ms =
                useful_work(*profile, workload.iterations);
            if (!finite_positive(linear_useful_work_ms))
            {
                return result;
            }

            const HardwareCharacteristics hardware = hardware_characteristics();
            const std::size_t logical_threads =
                std::max<std::size_t>(1, hardware.logical_threads);
            const std::size_t maximum_workers = std::max<std::size_t>(
                1,
                std::min(workload.iterations, logical_threads));

            const double confidence = prediction_confidence(*profile);
            const PerformanceModel performance_model =
                PerformanceModelBuilder().build(analysis);
            const WorkloadFamilyClassification family_classification =
                WorkloadFamilyClassifier().classify(
                    analysis,
                    performance_model,
                    profile);
            const RuntimeScalingResult scaling =
                global_config().enable_learned_runtime_scaling
                    ? RuntimeScalingPolicy().evaluate(
                        linear_useful_work_ms,
                        workload.iterations,
                        *profile,
                        family_classification)
                    : RuntimeScalingResult{};
            const double useful_work_ms =
                global_config().enable_learned_runtime_scaling
                    ? scaling.scaled_total_work_ms
                    : linear_useful_work_ms;
            const WorkloadFingerprint experience_fingerprint =
                fingerprint(workload, profile);
            const double framework_ms =
                std::max(0.0, profile->profiling_elapsed_ms);

            const std::vector<std::size_t> worker_counts =
                adaptive_worker_counts(maximum_workers);

            result.candidates.reserve(1 + worker_counts.size() * 3);
            result.candidates.push_back(sequential_candidate(
                useful_work_ms,
                framework_ms,
                confidence));

            for (std::size_t workers : worker_counts)
            {
                const std::size_t dynamic_chunk = adaptive_chunk_size(
                    workload.iterations,
                    workers,
                    *profile);

                result.candidates.push_back(parallel_candidate(
                    ExecutionEngineType::ThreadPool,
                    ExecutionStrategy::DynamicChunks,
                    workers,
                    dynamic_chunk,
                    useful_work_ms,
                    framework_ms,
                    workload.iterations,
                    analysis,
                    *profile,
                    confidence));

                result.candidates.push_back(parallel_candidate(
                    ExecutionEngineType::StaticThread,
                    ExecutionStrategy::StaticChunks,
                    workers,
                    0,
                    useful_work_ms,
                    framework_ms,
                    workload.iterations,
                    analysis,
                    *profile,
                    confidence));

                result.candidates.push_back(parallel_candidate(
                    ExecutionEngineType::OneTbb,
                    ExecutionStrategy::DynamicChunks,
                    workers,
                    dynamic_chunk,
                    useful_work_ms,
                    framework_ms,
                    workload.iterations,
                    analysis,
                    *profile,
                    confidence));
            }

            for (PlanCostEstimate& candidate : result.candidates)
            {
                candidate.runtime_scaling_applied = scaling.applied;
                candidate.runtime_scaling_linear_work_ms = linear_useful_work_ms;
                candidate.runtime_scaling_factor = scaling.correction_factor;
                candidate.runtime_scaling_exponent = scaling.scaling_exponent;
                candidate.runtime_scaling_confidence = scaling.confidence;
                candidate.runtime_scaling_extrapolation_ratio =
                    scaling.extrapolation_ratio;
            }

            if (global_config().enable_family_specific_calibration)
            {
                FamilyCalibrationPolicy family_policy;
                for (PlanCostEstimate& candidate : result.candidates)
                {
                    family_policy.apply(
                        candidate,
                        family_classification,
                        analysis,
                        performance_model);
                }
            }
            else
            {
                for (PlanCostEstimate& candidate : result.candidates)
                {
                    candidate.workload_family = family_classification.family;
                    candidate.workload_family_confidence =
                        family_classification.confidence;
                }
            }

            if (global_config().enable_memory_bandwidth_calibration)
            {
                MemoryBandwidthCalibrationPolicy bandwidth_policy;
                for (PlanCostEstimate& candidate : result.candidates)
                {
                    bandwidth_policy.apply(
                        candidate,
                        family_classification,
                        analysis,
                        performance_model);
                }
            }

            if (global_config().enable_memory_access_calibration)
            {
                MemoryAccessCalibrationPolicy access_policy;
                for (PlanCostEstimate& candidate : result.candidates)
                {
                    access_policy.apply(
                        candidate,
                        family_classification,
                        analysis,
                        performance_model);
                }
            }

            if (global_config().enable_residual_correction)
            {
                ResidualCorrectionPolicy residual_policy;
                for (PlanCostEstimate& candidate : result.candidates)
                {
                    residual_policy.apply(candidate, experience_fingerprint);
                }
            }
            else if (global_config().enable_prediction_calibration)
            {
                // Compatibility fallback for applications that explicitly
                // disable the Phase 6C residual layer.
                for (PlanCostEstimate& candidate : result.candidates)
                {
                    apply_experience_calibration(
                        candidate,
                        experience_fingerprint);
                }
            }

            CandidateRanker().rank(
                result.candidates,
                experience_fingerprint);

            if (global_config().enable_confidence_model)
                ConfidenceModel::apply(result.candidates);

            const auto best = std::min_element(
                result.candidates.begin(),
                result.candidates.end(),
                [](const PlanCostEstimate& left, const PlanCostEstimate& right)
                {
                    const double left_score = left.ranking_score > 0.0
                        ? left.ranking_score
                        : left.predicted_total_ms;
                    const double right_score = right.ranking_score > 0.0
                        ? right.ranking_score
                        : right.predicted_total_ms;
                    return left_score < right_score;
                });

            if (best == result.candidates.end() || !best->available)
            {
                return result;
            }

            result.available = true;
            result.recommended_plan = best->plan;
            result.recommended_total_ms = best->predicted_total_ms;
            result.confidence = best->confidence;
            return result;
        }

    private:
        static std::vector<std::size_t> adaptive_worker_counts(
            std::size_t maximum_workers)
        {
            std::vector<std::size_t> counts;
            if (maximum_workers <= 1)
                return counts;

            if (!global_config().enable_adaptive_execution_candidates)
            {
                counts.push_back(maximum_workers);
                return counts;
            }

            const std::size_t minimum_workers = std::max<std::size_t>(
                2,
                global_config().minimum_adaptive_workers);

            for (std::size_t workers = minimum_workers;
                 workers < maximum_workers;)
            {
                counts.push_back(workers);
                if (workers > maximum_workers / 2)
                    break;
                workers *= 2;
            }

            if (counts.empty() || counts.back() != maximum_workers)
                counts.push_back(maximum_workers);

            counts.erase(
                std::unique(counts.begin(), counts.end()),
                counts.end());
            return counts;
        }

        static double representative_iteration_cost(
            const FunctionProfile& profile)
        {
            double cost = profile.steady_state_ms_per_iteration;
            if (!finite_positive(cost))
                cost = profile.trimmed_mean_ms_per_iteration;
            if (!finite_positive(cost))
                cost = profile.median_ms_per_iteration;
            if (!finite_positive(cost))
                cost = profile.avg_ms_per_iteration;
            return finite_positive(cost) ? cost : 0.0;
        }

        static std::size_t adaptive_chunk_size(
            std::size_t iterations,
            std::size_t workers,
            const FunctionProfile& profile)
        {
            if (iterations == 0)
                return 1;

            const Config& config = global_config();
            const double per_iteration_ms =
                representative_iteration_cost(profile);

            std::size_t chunk = 1;
            if (finite_positive(per_iteration_ms))
            {
                const double desired =
                    config.target_dynamic_chunk_ms / per_iteration_ms;
                if (std::isfinite(desired) && desired > 1.0)
                {
                    chunk = static_cast<std::size_t>(std::min(
                        desired,
                        static_cast<double>(
                            std::numeric_limits<std::size_t>::max())));
                }
            }

            // Keep enough chunks for dynamic balancing while preventing tiny
            // callbacks from generating millions of scheduler operations.
            const std::size_t maximum_balanced_chunk = std::max<std::size_t>(
                1,
                (iterations + workers * 8 - 1) / (workers * 8));
            chunk = std::min(chunk, maximum_balanced_chunk);
            chunk = std::clamp(
                chunk,
                std::max<std::size_t>(1, config.minimum_dynamic_chunk_size),
                std::max<std::size_t>(
                    config.minimum_dynamic_chunk_size,
                    config.maximum_dynamic_chunk_size));
            return std::min(chunk, iterations);
        }

        static void apply_experience_calibration(
            PlanCostEstimate& estimate,
            const WorkloadFingerprint& fingerprint)
        {
            const ExperienceEntry* entry =
                global_experience_database().find_plan(
                    fingerprint,
                    estimate.plan);

            if (entry == nullptr ||
                entry->prediction_sample_count <
                    global_config().minimum_calibration_samples ||
                entry->confidence <
                    global_config().minimum_calibration_confidence)
            {
                return;
            }

            const double factor = std::clamp(
                entry->average_runtime_correction,
                0.50,
                2.00);

            estimate.uncalibrated_total_ms =
                estimate.predicted_total_ms;
            estimate.calibration_applied = true;
            estimate.calibration_factor = factor;
            estimate.calibration_samples =
                entry->prediction_sample_count;

            const double runtime_without_framework = std::max(
                0.0,
                estimate.predicted_total_ms -
                    estimate.framework_overhead_ms);

            estimate.predicted_total_ms =
                runtime_without_framework * factor +
                estimate.framework_overhead_ms;

            estimate.predicted_execution_ms *= factor;
            estimate.memory_penalty_ms *= factor;
            estimate.imbalance_penalty_ms *= factor;
            estimate.scheduling_overhead_ms *= factor;

            estimate.confidence = std::clamp(
                std::max(estimate.confidence, entry->confidence),
                0.0,
                1.0);
        }

        static bool finite_positive(double value)
        {
            return std::isfinite(value) && value > 0.0;
        }

        static double useful_work(
            const FunctionProfile& profile,
            std::size_t iterations)
        {
            return ProfileCostCalibrationPolicy()
                .evaluate(profile, iterations)
                .total_work_ms;
        }

        static double prediction_confidence(const FunctionProfile& profile)
        {
            double confidence = 0.35;

            switch (profile.metadata.confidence)
            {
            case ObservationConfidence::High:
                confidence = 0.90;
                break;
            case ObservationConfidence::Medium:
                confidence = 0.70;
                break;
            case ObservationConfidence::Low:
                confidence = 0.45;
                break;
            case ObservationConfidence::Unavailable:
                confidence = 0.25;
                break;
            }

            if (!profile.measurement_reliable)
            {
                confidence *= 0.65;
            }

            if (profile.stop_reason == ProfileStopReason::TimeBudgetReached ||
                profile.stop_reason == ProfileStopReason::InvocationBudgetReached ||
                profile.stop_reason == ProfileStopReason::MeasurementUnreliable)
            {
                confidence *= 0.80;
            }

            if (profile.measured_batches < 4)
            {
                confidence *= 0.75;
            }

            return std::clamp(confidence, 0.0, 1.0);
        }

        static PlanCostEstimate sequential_candidate(
            double useful_work_ms,
            double framework_ms,
            double confidence)
        {
            PlanCostEstimate estimate;
            estimate.available = true;
            estimate.plan.engine = ExecutionEngineType::ThreadPool;
            estimate.plan.strategy = ExecutionStrategy::Sequential;
            estimate.plan.parallel = false;
            estimate.plan.job_count = 1;
            estimate.useful_work_ms = useful_work_ms;
            estimate.predicted_execution_ms = useful_work_ms;
            estimate.framework_overhead_ms = framework_ms;
            estimate.predicted_parallel_efficiency = 1.0;
            estimate.predicted_speedup = 1.0;
            estimate.predicted_total_ms =
                estimate.predicted_execution_ms + framework_ms;
            estimate.confidence = confidence;
            return estimate;
        }

        static double backend_efficiency(ExecutionEngineType engine)
        {
            switch (engine)
            {
            case ExecutionEngineType::OneTbb:
                return 0.82;
            case ExecutionEngineType::ThreadPool:
                return 0.74;
            case ExecutionEngineType::StaticThread:
                return 0.78;
            case ExecutionEngineType::Auto:
                break;
            }

            return 0.70;
        }

        static double scheduling_overhead(
            ExecutionEngineType engine,
            std::size_t workers,
            std::size_t chunk_size,
            std::size_t iterations,
            const FunctionProfile& profile)
        {
            if (global_config().enable_machine_runtime_calibration)
            {
                const double measured = calibrated_backend_overhead_ms(
                    engine,
                    iterations,
                    workers,
                    chunk_size);
                if (std::isfinite(measured) && measured >= 0.0)
                {
                    return measured;
                }
            }

            const double measured_reference =
                std::max(0.01, profile.estimated_parallel_overhead_ms);

            switch (engine)
            {
            case ExecutionEngineType::ThreadPool:
                return measured_reference * 0.35 +
                    static_cast<double>(workers) * 0.0015;
            case ExecutionEngineType::StaticThread:
                return measured_reference +
                    static_cast<double>(workers) * 0.0125;
            case ExecutionEngineType::OneTbb:
                return measured_reference * 0.45 +
                    std::log2(static_cast<double>(workers) + 1.0) * 0.004;
            case ExecutionEngineType::Auto:
                break;
            }

            return measured_reference;
        }

        static RuntimeWorkloadClass runtime_workload_class(
            const WorkloadAnalysis& analysis,
            const FunctionProfile& profile)
        {
            const double iteration_cost =
                representative_iteration_cost(profile);

            bool all_contiguous = !analysis.structural.dimensions.empty();
            for (const DimensionAnalysis& dimension :
                 analysis.structural.dimensions)
            {
                if (!dimension.contiguous_known || !dimension.contiguous)
                {
                    all_contiguous = false;
                    break;
                }
            }

            const bool large_relative_to_cache =
                analysis.structural.cache_ratios_available &&
                analysis.structural.l3_residency_ratio >= 0.5;
            const bool cheap_callback =
                iteration_cost > 0.0 && iteration_cost < 0.00020;

            if (all_contiguous && large_relative_to_cache && cheap_callback)
            {
                return RuntimeWorkloadClass::StreamingLike;
            }

            if (iteration_cost >= 0.001 &&
                profile.coefficient_of_variation <= 0.35)
            {
                return RuntimeWorkloadClass::ComputeLike;
            }

            return RuntimeWorkloadClass::General;
        }

        static double memory_efficiency_factor(
            const WorkloadAnalysis& analysis,
            const FunctionProfile& profile,
            std::size_t workers)
        {
            double factor = 1.0;

            if (analysis.structural.cache_ratios_available)
            {
                const double pressure =
                    std::max(0.0, analysis.structural.l3_residency_ratio);
                if (pressure > 1.0)
                {
                    factor /= 1.0 + 0.10 * std::log2(pressure + 1.0);
                }
            }

            const double iteration_cost =
                profile.steady_state_ms_per_iteration > 0.0
                    ? profile.steady_state_ms_per_iteration
                    : profile.trimmed_mean_ms_per_iteration;

            const bool cheap_streaming_candidate =
                iteration_cost > 0.0 &&
                iteration_cost < 0.00020 &&
                analysis.structural.cache_ratios_available &&
                analysis.structural.l3_residency_ratio >= 0.5;

            if (cheap_streaming_candidate)
            {
                factor *= 0.45;
            }

            if (global_config().enable_hardware_aware_prediction)
            {
                const HardwarePredictionContext context =
                    hardware_prediction_context(
                        analysis,
                        hardware_characteristics(),
                        workers);
                factor *= context.bandwidth_parallelism_limit;

                if (context.l2_pressure_per_worker > 1.0)
                {
                    factor /= 1.0 +
                        global_config().hardware_l2_pressure_penalty *
                        std::log2(context.l2_pressure_per_worker + 1.0);
                }
                factor *= 1.0 -
                    global_config().hardware_numa_penalty * context.numa_pressure;
            }

            return std::clamp(factor, 0.18, 1.0);
        }

        static double imbalance_fraction(
            ExecutionStrategy strategy,
            const FunctionProfile& profile)
        {
            const double cv = std::max(0.0, profile.coefficient_of_variation);
            const double tail = std::max(0.0, profile.tail_ratio - 1.0);
            const double regional = std::max(0.0, profile.regional_cost_ratio - 1.0);

            const double raw =
                cv * 0.30 + tail * 0.10 + regional * 0.08;

            const double strategy_factor =
                strategy == ExecutionStrategy::DynamicChunks ? 0.35 : 1.0;

            return std::clamp(raw * strategy_factor, 0.0, 0.75);
        }

        static PlanCostEstimate parallel_candidate(
            ExecutionEngineType engine,
            ExecutionStrategy strategy,
            std::size_t workers,
            std::size_t chunk_size,
            double useful_work_ms,
            double framework_ms,
            std::size_t iterations,
            const WorkloadAnalysis& analysis,
            const FunctionProfile& profile,
            double confidence)
        {
            PlanCostEstimate estimate;
            estimate.available = true;
            estimate.plan.engine = engine;
            estimate.plan.strategy = strategy;
            estimate.plan.parallel = true;
            estimate.plan.job_count = workers;
            estimate.plan.chunk_size =
                strategy == ExecutionStrategy::DynamicChunks
                    ? std::max<std::size_t>(1, chunk_size)
                    : 0;
            estimate.useful_work_ms = useful_work_ms;
            estimate.framework_overhead_ms = framework_ms;

            const double memory_factor =
                memory_efficiency_factor(analysis, profile, workers);
            const double analytical_efficiency =
                backend_efficiency(engine) * memory_factor;

            const double worker_span =
                static_cast<double>(workers > 0 ? workers - 1 : 0);
            const double contention =
                (1.0 - memory_factor) * 0.10 +
                (engine == ExecutionEngineType::StaticThread ? 0.018 : 0.010);
            const double analytical_speedup = std::max(
                1.0,
                1.0 +
                    (worker_span * analytical_efficiency) /
                    (1.0 + contention * worker_span));

            double calibrated_speedup = analytical_speedup;
            if (global_config().enable_machine_runtime_calibration)
            {
                calibrated_speedup = calibrated_backend_speedup(
                    engine,
                    workers,
                    runtime_workload_class(analysis, profile));
            }

            // The machine probe is the primary signal, while the analytical
            // model prevents one small synthetic probe from dominating.
            estimate.predicted_speedup = std::max(
                1.0,
                calibrated_speedup * 0.75 + analytical_speedup * 0.25);
            estimate.predicted_parallel_efficiency = std::clamp(
                (estimate.predicted_speedup - 1.0) /
                    std::max(1.0, worker_span),
                0.0,
                1.0);

            estimate.predicted_execution_ms =
                useful_work_ms / estimate.predicted_speedup;

            const double ideal_execution_ms =
                useful_work_ms /
                std::max(1.0, 1.0 +
                    (static_cast<double>(workers) - 1.0) *
                    backend_efficiency(engine));

            estimate.memory_penalty_ms =
                std::max(0.0, estimate.predicted_execution_ms - ideal_execution_ms);

            estimate.imbalance_penalty_ms =
                estimate.predicted_execution_ms *
                imbalance_fraction(strategy, profile);

            estimate.scheduling_overhead_ms =
                scheduling_overhead(
                    engine,
                    workers,
                    estimate.plan.chunk_size,
                    iterations,
                    profile);
            estimate.machine_calibration_used =
                global_config().enable_machine_runtime_calibration;

            estimate.predicted_total_ms =
                estimate.predicted_execution_ms +
                estimate.imbalance_penalty_ms +
                estimate.scheduling_overhead_ms +
                estimate.framework_overhead_ms;

            estimate.confidence = confidence;
            return estimate;
        }
    };
}
