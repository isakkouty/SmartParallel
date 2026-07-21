#pragma once

#include <algorithm>
#include <cstddef>
#include <smart/core/config.hpp>
#include <smart/core/statistics.hpp>
#include <smart/core/timing_report.hpp>
#include <smart/core/timing_scope.hpp>
#include <smart/decision/decision.hpp>
#include <smart/execution/execution_context.hpp>
#include <smart/execution/nested_execution_coordinator.hpp>
#include <smart/execution/executor.hpp>
#include <smart/execution/static_container_engine.hpp>
#include <smart/execution/static_thread_engine.hpp>
#include <smart/experience/runtime_experience.hpp>
#include <smart/hardware/hardware.hpp>
#include <smart/profiling/function_profile_cache.hpp>
#include <smart/profiling/isolated_function_profile.hpp>
#include <smart/workload/fingerprint.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_builder.hpp>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace smart
{
struct ParallelForProfileDiagnostics
{
    bool profile_available = false;

    bool cache_hit = false;
    bool sequential_fast_path = false;
    std::size_t sampled_iterations = 0;
    double estimated_sequential_ms = 0.0;
    double estimated_parallel_ms = 0.0;
    double predicted_speedup = 0.0;

    double cache_lookup_ms = 0.0;

    double workload_analysis_ms = 0.0;
    double profiling_ms = 0.0;
    double decision_ms = 0.0;

    double execution_ms = 0.0;
    double total_ms = 0.0;
};

inline ParallelForProfileDiagnostics& global_last_parallel_for_profile_diagnostics()
{
    static thread_local ParallelForProfileDiagnostics diagnostics;
    return diagnostics;
}

struct ParallelForNestedDiagnostics
{
    bool coordinated = false;
    ExecutionEngineType requested_engine = ExecutionEngineType::Auto;
    ExecutionEngineType selected_engine = ExecutionEngineType::Auto;
    NestedExecutionPolicy policy = NestedExecutionPolicy::NotNested;
    NestedExecutionMechanism mechanism = NestedExecutionMechanism::DirectExecution;
    std::size_t parent_depth = 0;
    std::size_t requested_budget = 1;
    std::size_t effective_budget = 1;
    bool same_runtime_domain = false;
    bool cross_backend_transition = false;
    bool budget_limited = false;
    bool granularity_limited = false;
    std::size_t granularity_budget = 1;
    bool chunk_size_tuned = false;
    std::size_t original_chunk_size = 0;
    std::size_t effective_chunk_size = 0;
};

inline ParallelForNestedDiagnostics& global_last_parallel_for_nested_diagnostics()
{
    static thread_local ParallelForNestedDiagnostics diagnostics;
    return diagnostics;
}

namespace detail
{
inline std::vector<std::size_t> parallel_for_sample_indices(std::size_t total)
{
    const auto& config = global_config();
    const std::size_t max_samples = std::min(
        total,
        std::max(config.parallel_for_profile_min_samples, config.parallel_for_profile_max_samples));
    if (max_samples == 0)
        return {};
    const std::size_t regions =
        std::max<std::size_t>(1, std::min(config.parallel_for_profile_regions, max_samples));
    std::vector<std::size_t> indices;
    indices.reserve(max_samples);
    for (std::size_t sample = 0; sample < max_samples; ++sample)
    {
        const std::size_t region = sample % regions;
        const std::size_t round = sample / regions;
        const std::size_t region_begin = (total * region) / regions;
        const std::size_t region_end = (total * (region + 1)) / regions;
        const std::size_t width = std::max<std::size_t>(1, region_end - region_begin);
        const std::size_t index = region_begin + std::min(round, width - 1);
        if (std::find(indices.begin(), indices.end(), index) == indices.end())
            indices.push_back(index);
    }
    return indices;
}

inline void update_parallel_cost_model(FunctionProfile& profile, std::size_t total_iterations)
{
    const HardwareCharacteristics hw = hardware_characteristics();
    const std::size_t workers = std::max<std::size_t>(1, hw.logical_threads);
    profile.estimated_total_work_ms =
        profile.avg_ms_per_iteration * static_cast<double>(total_iterations);
    profile.estimated_parallel_overhead_ms = global_config().parallel_for_estimated_overhead_ms;
    const double parallel_ms = profile.estimated_parallel_overhead_ms
                               + (profile.estimated_total_work_ms / static_cast<double>(workers))
                                     * global_config().parallel_for_imbalance_penalty;
    profile.parallel_worthiness =
        parallel_ms > 0.0 ? profile.estimated_total_work_ms / parallel_ms : 0.0;
    auto& d = global_last_parallel_for_profile_diagnostics();
    d.profile_available = profile.available;
    d.estimated_sequential_ms = profile.estimated_total_work_ms;
    d.estimated_parallel_ms = parallel_ms;
    d.predicted_speedup = profile.parallel_worthiness;
}

inline FunctionProfile make_parallel_for_profile(std::size_t total_iterations,
                                                 std::size_t sampled_iterations,
                                                 double elapsed_ms)
{
    FunctionProfile profile;
    if (sampled_iterations == 0)
    {
        profile.unavailable_reason = FunctionProfileUnavailableReason::EmptyRange;
        return profile;
    }

    // Even an ultra-cheap callback is useful evidence. On Windows the
    // measured batch may fall below the configured reliability signal
    // (or even round to zero), but discarding it entirely prevents a
    // cheap sequential profile from ever accumulating observations.
    // Keep the profile available, mark it unreliable/low-confidence,
    // and require repeated independent observations before the cached
    // sequential fast path can activate.
    const double measured_ms = std::max(0.0, elapsed_ms);
    const bool reliable_signal =
        measured_ms >= global_config().parallel_for_profile_min_signal_ms;

    // The configured reliability threshold controls confidence, but callers may
    // deliberately raise it to demand repeated observations. Do not let such a
    // threshold suppress clearly measurable contradictory evidence forever.
    //
    // Cost extrapolation therefore uses a small, bounded timer-noise floor:
    // sub-floor measurements are treated as a lower bound for the whole range,
    // while measurements above the floor are safe to extrapolate. This avoids
    // amplifying near-zero timer noise for empty callbacks and still lets a
    // periodically revalidated expensive callback escape a cached sequential
    // classification immediately.
    constexpr double maximum_extrapolation_noise_floor_ms = 0.05;
    const double extrapolation_signal_ms =
        std::min(global_config().parallel_for_profile_min_signal_ms,
                 maximum_extrapolation_noise_floor_ms);
    const bool extrapolate_cost = measured_ms >= extrapolation_signal_ms;
    const double modeled_iterations =
        extrapolate_cost ? static_cast<double>(sampled_iterations)
                         : static_cast<double>(total_iterations);

    const double per_iteration_ms = measured_ms / modeled_iterations;

    profile.available = true;
    profile.sampling_mode = FunctionProfileSamplingMode::Direct;
    profile.unavailable_reason = FunctionProfileUnavailableReason::None;
    profile.metadata.source = ObservationSource::Sampled;
    profile.metadata.confidence =
        reliable_signal ? ObservationConfidence::Medium : ObservationConfidence::Low;
    profile.stop_reason = ProfileStopReason::InvocationBudgetReached;
    profile.samples = sampled_iterations;
    profile.measured_batches = 1;
    profile.callback_invocations = sampled_iterations;
    profile.chosen_batch_size = sampled_iterations;
    profile.profiling_elapsed_ms = measured_ms;
    profile.measurement_reliable = reliable_signal;
    profile.avg_ms_per_iteration = per_iteration_ms;
    profile.median_ms_per_iteration = per_iteration_ms;
    profile.trimmed_mean_ms_per_iteration = per_iteration_ms;
    profile.p95_ms_per_iteration = per_iteration_ms;
    profile.max_ms_per_iteration = per_iteration_ms;
    profile.tail_ratio = 1.0;
    profile.instability_ratio = 1.0;
    profile.stable = true;
    update_parallel_cost_model(profile, total_iterations);
    return profile;
}
} // namespace detail

template <typename Function>
void parallel_for(std::size_t begin, std::size_t end, Function func)
{
    if (end < begin)
        throw std::invalid_argument("SmartParallel parallel_for end must not precede begin");
    const std::size_t total = end - begin;
    ExecutionContext execution_context = detail::make_execution_context();
    const ExecutionContext parent_execution_context = current_execution_context();

    // Profiling invokes user callbacks before the final execution plan exists.
    // Treat that phase as a sequential region while preserving the complete
    // ancestor/runtime lineage; descendants must never observe a half-built
    // context or accidentally assume a new parallel runtime domain.
    execution_context.engine = ExecutionEngineType::Auto;
    execution_context.parallel = false;
    execution_context.nested_policy = NestedExecutionPolicy::NotNested;
    execution_context.concurrency_budget = 1;
    inherit_execution_lineage(execution_context, parent_execution_context);

    auto invoke = [&](std::size_t index)
    {
        detail::ExecutionContextScope context_scope(execution_context);
        func(index);
    };
    global_last_parallel_for_profile_diagnostics() = ParallelForProfileDiagnostics{};
    global_last_parallel_for_nested_diagnostics() = ParallelForNestedDiagnostics{};
    if (total == 0)
    {
        global_last_decision_report() = DecisionReport{};
        return;
    }
    if (global_config().enable_timing_diagnostics)
        clear_timing_report();

    TimingScope total_scope("total");
    Timer whole_call_timer;
    FunctionProfile function_profile;
    std::vector<std::size_t> sampled_indices;

    using Callable = std::decay_t<Function>;
    const FunctionProfileKey cache_key{
        typeid(Callable).hash_code(), sizeof(std::size_t), iteration_bucket(total)};
    std::optional<CachedFunctionProfile> cached_profile;
    bool cache_revalidation_due = false;
    if (global_config().enable_parallel_for_profile_cache)
    {
        Timer cache_lookup_timer;
        cached_profile = global_function_profile_cache().find(cache_key);
        global_last_parallel_for_profile_diagnostics().cache_lookup_ms =
            cache_lookup_timer.elapsed_ms();
        if (cached_profile
            && cached_profile->hits >= global_config().parallel_for_profile_cache_min_hits)
        {
            const auto& config = global_config();
            // Any cached profile below the normal parallel break-even threshold is a
            // sequential candidate and must accumulate independent observations before
            // it may be reused. The stricter speedup margin is applied later only when
            // deciding whether the confirmed profile qualifies for the direct fast path.
            const bool cached_predicts_sequential =
                cached_profile->profile.parallel_worthiness
                < config.parallel_for_minimum_predicted_speedup;
            // A sub-signal measurement is not trustworthy enough to classify as
            // parallel, even when timer quantization produces an optimistic speedup.
            // Require independent observations for either a sequential prediction
            // or an unreliable measurement before allowing cache reuse.
            const bool cached_requires_confirmation =
                cached_predicts_sequential || !cached_profile->profile.measurement_reliable;
            const bool needs_confirmation =
                cached_requires_confirmation
                && cached_profile->observations
                       < config.parallel_for_sequential_fast_path_min_observations;
            const bool periodic_revalidation =
                cached_predicts_sequential
                && config.parallel_for_sequential_fast_path_revalidate_interval > 0
                && cached_profile->sequential_fast_path_uses
                       >= config.parallel_for_sequential_fast_path_revalidate_interval;
            cache_revalidation_due = needs_confirmation || periodic_revalidation;

            if (!cache_revalidation_due)
            {
                function_profile = cached_profile->profile;
                detail::update_parallel_cost_model(function_profile, total);
                global_last_parallel_for_profile_diagnostics().cache_hit = true;
            }
        }
    }

    // Sequential cache entries are deliberately non-sticky. The bypass is
    // enabled only after multiple independent observations agree, only for a
    // prediction comfortably below break-even, and only until periodic
    // regional revalidation is due. A contradictory sample replaces the old
    // classification immediately in FunctionProfileCache::store().
    const auto& profile_diagnostics = global_last_parallel_for_profile_diagnostics();
    const bool cached_cost_evidence =
        profile_diagnostics.cache_hit && cached_profile && function_profile.samples > 0;
    const double fast_path_threshold =
        global_config().parallel_for_minimum_predicted_speedup
        * global_config().parallel_for_sequential_fast_path_speedup_margin;
    const bool high_confidence =
        cached_profile
        && cached_profile->observations
            >= global_config().parallel_for_sequential_fast_path_min_observations
        && function_profile.stable
        && function_profile.metadata.confidence == ObservationConfidence::High;
    if (cached_cost_evidence && high_confidence
        && global_config().enable_parallel_for_cached_sequential_fast_path
        && function_profile.parallel_worthiness < fast_path_threshold)
    {
        auto& diagnostics = global_last_parallel_for_profile_diagnostics();
        diagnostics.sequential_fast_path = true;
        global_function_profile_cache().note_sequential_fast_path_use(cache_key);

        DecisionReport report{};
        report.has_function_profile = true;
        report.function_profile = function_profile;
        report.plan.parallel = false;
        report.plan.strategy = ExecutionStrategy::Sequential;
        report.plan.job_count = 1;
        global_last_decision_report() = report;

        execution_context.engine = ExecutionEngineType::Auto;
        execution_context.parallel = false;
        execution_context.nested_policy = NestedExecutionPolicy::NotNested;
        execution_context.concurrency_budget = 1;
        inherit_execution_lineage(execution_context, parent_execution_context);

        Timer execution_timer;
        for (std::size_t i = begin; i < end; ++i)
            invoke(i);
        diagnostics.execution_ms = execution_timer.elapsed_ms();
        diagnostics.total_ms = whole_call_timer.elapsed_ms();

        const ExecutionStats stats{total, diagnostics.total_ms};
        if (global_config().enable_experience)
        {
            const Workload workload = WorkloadBuilder::index_range(total);
            record_execution_experience(workload,
                                        &function_profile,
                                        global_last_decision_report(),
                                        report.plan,
                                        stats.elapsed_ms);
        }
        return;
    }

    Timer analysis_timer;
    Workload workload = WorkloadBuilder::index_range(total);
    WorkloadAnalyzer analyzer;
    WorkloadAnalysis analysis = analyzer.analyze(workload);
    global_last_parallel_for_profile_diagnostics().workload_analysis_ms =
        analysis_timer.elapsed_ms();

    if (!function_profile.available && global_config().enable_parallel_for_auto_profiling)
    {
        Timer profiling_timer;
        sampled_indices = detail::parallel_for_sample_indices(total);
        Timer sample_timer;
        std::size_t executed = 0;
        for (const std::size_t index : sampled_indices)
        {
            invoke(begin + index);
            ++executed;
            if (executed >= global_config().parallel_for_profile_min_samples
                && sample_timer.elapsed_ms() >= global_config().parallel_for_profile_min_signal_ms)
                break;
        }
        sampled_indices.resize(executed);
        function_profile =
            detail::make_parallel_for_profile(total, executed, sample_timer.elapsed_ms());
        std::sort(sampled_indices.begin(), sampled_indices.end());
        global_last_parallel_for_profile_diagnostics().sampled_iterations = executed;
        global_last_parallel_for_profile_diagnostics().profiling_ms = profiling_timer.elapsed_ms();
        if (global_config().enable_parallel_for_profile_cache)
            global_function_profile_cache().store(
                cache_key,
                function_profile,
                global_config().parallel_for_profile_cache_blend,
                global_config().parallel_for_minimum_predicted_speedup,
                global_config().parallel_for_sequential_fast_path_min_observations);
    }

    Timer decision_timer;
    DecisionEngine engine;
    const FunctionProfile* profile_ptr = function_profile.available ? &function_profile : nullptr;
    ExecutionPlan requested_plan = engine.decide(workload, analysis, profile_ptr);

    // An explicit configured backend constrains backend selection without
    // bypassing the normal decision about whether the workload is worth
    // parallelizing. This makes the public parallel_for path the single
    // integration point for every backend and nested mechanism.
    if (requested_plan.parallel && global_config().execution_engine != ExecutionEngineType::Auto)
    {
        requested_plan.engine = global_config().execution_engine;
        requested_plan.strategy = requested_plan.engine == ExecutionEngineType::StaticThread
                                      ? ExecutionStrategy::StaticChunks
                                      : ExecutionStrategy::DynamicChunks;
    }

    NestedExecutionDecision nested_decision =
        NestedExecutionCoordinator{}.coordinate(parent_execution_context, requested_plan);
    if (global_config().enable_nested_granularity_enforcement
        && nested_decision.negotiation.nested_request)
    {
        NestedExecutionConstraints constraints;
        constraints.iteration_count = total - sampled_indices.size();
        constraints.minimum_iterations_per_worker =
            global_config().nested_min_iterations_per_worker;
        constraints.minimum_chunks_per_worker =
            global_config().nested_min_chunks_per_worker;
        constraints.target_chunks_per_worker =
            global_config().nested_target_chunks_per_worker;
        nested_decision =
            NestedExecutionCoordinator{}.enforce_constraints(nested_decision, constraints);
    }
    ExecutionPlan plan = nested_decision.plan;
    const NestedExecutionPolicy nested_policy = nested_decision.policy;
    execution_context.engine = plan.parallel ? resolve_execution_engine_type(plan.engine)
                                             : ExecutionEngineType::Auto;
    execution_context.parallel = plan.parallel;
    execution_context.nested_policy = nested_policy;
    execution_context.concurrency_budget = nested_decision.effective_budget;
    inherit_execution_lineage(execution_context, parent_execution_context);

    global_last_parallel_for_profile_diagnostics().decision_ms = decision_timer.elapsed_ms();
    global_last_decision_report() = engine.last_report();
    global_last_decision_report().plan = plan;

    auto& nested_diagnostics = global_last_parallel_for_nested_diagnostics();
    nested_diagnostics.coordinated = true;
    nested_diagnostics.requested_engine = requested_plan.engine;
    nested_diagnostics.selected_engine = plan.parallel
                                             ? resolve_execution_engine_type(plan.engine)
                                             : requested_plan.engine;
    nested_diagnostics.policy = nested_decision.policy;
    nested_diagnostics.mechanism = nested_decision.negotiation.mechanism;
    nested_diagnostics.parent_depth = parent_execution_context.depth;
    nested_diagnostics.requested_budget = nested_decision.requested_budget;
    nested_diagnostics.effective_budget = nested_decision.effective_budget;
    nested_diagnostics.same_runtime_domain = nested_decision.negotiation.same_runtime_domain;
    nested_diagnostics.cross_backend_transition =
        nested_decision.negotiation.cross_backend_transition;
    nested_diagnostics.budget_limited = nested_decision.budget_limited;
    nested_diagnostics.granularity_limited = nested_decision.granularity_limited;
    nested_diagnostics.granularity_budget = nested_decision.granularity_budget;
    nested_diagnostics.chunk_size_tuned = nested_decision.chunk_size_tuned;
    nested_diagnostics.original_chunk_size = nested_decision.original_chunk_size;
    nested_diagnostics.effective_chunk_size = nested_decision.effective_chunk_size;

    (void)nested_policy;
    Timer execution_timer;
    std::size_t cursor = 0;
    auto execute_gap = [&](std::size_t gap_begin, std::size_t gap_end)
    {
        if (gap_end <= gap_begin)
            return;
        Workload gap_workload = WorkloadBuilder::index_range(gap_end - gap_begin);
        execute_workload(gap_workload,
                         plan,
                         [&](std::size_t i)
                         {
                             invoke(begin + gap_begin + i);
                         },
                         nested_policy);
    };
    for (const std::size_t sampled_index : sampled_indices)
    {
        execute_gap(cursor, sampled_index);
        cursor = sampled_index + 1;
    }
    execute_gap(cursor, total);
    auto& final_diagnostics = global_last_parallel_for_profile_diagnostics();
    final_diagnostics.execution_ms = execution_timer.elapsed_ms();
    final_diagnostics.total_ms = whole_call_timer.elapsed_ms();

    const ExecutionStats stats{total, final_diagnostics.total_ms};
    if (global_config().enable_experience)
        record_execution_experience(
            workload, profile_ptr, global_last_decision_report(), plan, stats.elapsed_ms);
}

template <typename Container, typename Function>
void for_each(Container& container, Function func)
{
    if (global_config().enable_timing_diagnostics)
    {
        clear_timing_report();
    }

    TimingScope total_scope("total");

    Workload workload;

    {
        TimingScope scope("workload_build");
        workload = WorkloadBuilder::container(container);
    }

    WorkloadAnalysis analysis;

    {
        TimingScope scope("workload_analysis");

        WorkloadAnalyzer analyzer;
        analysis = analyzer.analyze(workload);
    }

    smart::FunctionProfile function_profile;

    {
        TimingScope scope("function_profile");

        smart::FunctionProfiler::Config profiler_config;
        profiler_config.min_samples = 4;
        profiler_config.max_samples = 8;
        profiler_config.batch_size = 8;
        profiler_config.measured_parallel_overhead_ms = 1.0;

        function_profile = profile_container_on_copies(container, func, profiler_config);
    }

    ExecutionPlan plan;

    {
        TimingScope scope("decision");

        DecisionEngine engine;
        plan = engine.decide(workload, analysis, &function_profile);
        global_last_decision_report() = engine.last_report();
    }

    if (plan.engine == ExecutionEngineType::StaticThread
        && plan.strategy == ExecutionStrategy::StaticChunks)
    {
        ExecutionStats stats;

        {
            TimingScope scope("execution_static_chunks");

            Timer timer;

            static_thread_for_each(container, plan.job_count, func);

            stats = ExecutionStats{workload.iterations, timer.elapsed_ms()};
        }

        if (global_config().enable_experience)
        {
            TimingScope scope("experience_record");

            record_execution_experience(
                workload, &function_profile, global_last_decision_report(), plan, stats.elapsed_ms);
        }

        return;
    }

    ExecutionStats stats;

    {
        TimingScope scope("execution");

        stats = execute_workload(workload,
                                 plan,
                                 [&](std::size_t i)
                                 {
                                     func(container[i]);
                                 });
    }

    if (global_config().enable_experience)
    {
        TimingScope scope("experience_record");

        record_execution_experience(
            workload, &function_profile, global_last_decision_report(), plan, stats.elapsed_ms);
    }
}

template <typename ContainerA, typename ContainerB, typename Function>
void for_each_pair(ContainerA& a, ContainerB& b, Function func)
{
    if (global_config().enable_timing_diagnostics)
    {
        clear_timing_report();
    }

    TimingScope total_scope("total");

    Workload workload;

    {
        TimingScope scope("workload_build");
        workload = WorkloadBuilder::pair_container(a, b);
    }

    if (workload.iterations_saturated)
    {
        throw std::overflow_error("SmartParallel pair workload iteration count overflow");
    }

    std::size_t size_b = static_cast<std::size_t>(b.size());

    WorkloadAnalysis analysis;

    {
        TimingScope scope("workload_analysis");

        WorkloadAnalyzer analyzer;
        analysis = analyzer.analyze(workload);
    }

    smart::FunctionProfile function_profile;

    {
        TimingScope scope("function_profile");

        smart::FunctionProfiler::Config profiler_config;
        profiler_config.min_samples = 4;
        profiler_config.max_samples = 8;
        profiler_config.batch_size = 8;
        profiler_config.measured_parallel_overhead_ms = 1.0;

        function_profile = profile_pair_on_copies(a, b, func, profiler_config);
    }

    ExecutionPlan plan;

    {
        TimingScope scope("decision");

        DecisionEngine engine;
        plan = engine.decide(workload, analysis, &function_profile);
        global_last_decision_report() = engine.last_report();
    }

    ExecutionStats stats;

    {
        TimingScope scope("execution");

        stats = execute_workload(workload,
                                 plan,
                                 [&](std::size_t k)
                                 {
                                     std::size_t i = k / size_b;
                                     std::size_t j = k % size_b;

                                     func(a[i], b[j]);
                                 });
    }

    if (global_config().enable_experience)
    {
        TimingScope scope("experience_record");

        record_execution_experience(
            workload, &function_profile, global_last_decision_report(), plan, stats.elapsed_ms);
    }
}
} // namespace smart
