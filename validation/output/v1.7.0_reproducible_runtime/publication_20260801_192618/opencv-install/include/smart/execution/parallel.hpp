#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <tuple>
#include <utility>
#include <smart/core/config.hpp>
#include <smart/core/statistics.hpp>
#include <smart/core/timing_report.hpp>
#include <smart/core/timing_scope.hpp>
#include <smart/decision/decision.hpp>
#include <smart/decision/backend_calibration.hpp>
#include <smart/execution/execution_context.hpp>
#include <smart/execution/execution_override.hpp>
#include <smart/runtime/detail/operation.hpp>
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
template <typename Function>
class ParallelCallsiteFunction
{
  public:
    ParallelCallsiteFunction(std::size_t callsite_key, Function function)
        : callsite_key_(callsite_key), function_(std::move(function))
    {
    }

    std::size_t smartparallel_callsite_key() const noexcept { return callsite_key_; }

    template <typename... Args>
    decltype(auto) operator()(Args&&... args)
    {
        return function_(std::forward<Args>(args)...);
    }

    template <typename WrappedFunction = Function>
    auto smartparallel_execute_sequential(std::size_t begin, std::size_t end)
        -> decltype(std::declval<WrappedFunction&>().smartparallel_execute_sequential(
                        begin, end),
                    void())
    {
        function_.smartparallel_execute_sequential(begin, end);
    }

  private:
    std::size_t callsite_key_ = 0;
    Function function_;
};

// Optional escape hatch for reusable functor types or std::function wrappers
// that appear at multiple semantically unrelated source callsites. Normal
// lambdas already have unique closure types and do not need an explicit key.
template <typename Function>
auto with_parallel_callsite(std::size_t callsite_key, Function&& function)
{
    return ParallelCallsiteFunction<std::decay_t<Function>>(
        callsite_key, std::forward<Function>(function));
}

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
template <typename T, typename = void>
struct HasTargetType : std::false_type
{
};

template <typename T>
struct HasTargetType<T, std::void_t<decltype(std::declval<const T&>().target_type())>>
    : std::true_type
{
};

template <typename T, typename = void>
struct HasSmartParallelCallsiteKey : std::false_type
{
};

template <typename T>
struct HasSmartParallelCallsiteKey<
    T,
    std::void_t<decltype(std::declval<const T&>().smartparallel_callsite_key())>>
    : std::true_type
{
};

template <typename T, typename = void>
struct HasSmartParallelSequentialRange : std::false_type
{
};

template <typename T>
struct HasSmartParallelSequentialRange<
    T,
    std::void_t<decltype(std::declval<T&>().smartparallel_execute_sequential(
        std::declval<std::size_t>(), std::declval<std::size_t>()))>>
    : std::true_type
{
};

template <typename Function>
void execute_parallel_for_sequential_range(Function& function,
                                           std::size_t begin,
                                           std::size_t end)
{
    if constexpr (HasSmartParallelSequentialRange<Function>::value)
    {
        function.smartparallel_execute_sequential(begin, end);
    }
    else
    {
        for (std::size_t index = begin; index < end; ++index)
            function(index);
    }
}

template <typename Function>
std::size_t callable_identity_hash(const Function& function) noexcept
{
    using Callable = std::decay_t<Function>;
    std::size_t hash = typeid(Callable).hash_code();
    const auto combine = [&hash](std::size_t value)
    {
        hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    };
    if constexpr (std::is_pointer_v<Callable>
                  && std::is_function_v<std::remove_pointer_t<Callable>>)
    {
        combine(reinterpret_cast<std::uintptr_t>(function));
    }
    else if constexpr (HasSmartParallelCallsiteKey<Callable>::value)
    {
        combine(function.smartparallel_callsite_key());
    }
    else if constexpr (HasTargetType<Callable>::value)
    {
        combine(function.target_type().hash_code());
    }
    return hash;
}

inline std::size_t parallel_policy_signature(const Config& config) noexcept
{
    std::size_t hash = 0x6a09e667f3bcc909ull;
    const auto combine = [&hash](std::size_t value)
    {
        hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    };
    const auto combine_double = [&combine](double value)
    {
        combine(std::hash<double>{}(value));
    };

    combine(config.enable_nested_granularity_enforcement ? 1u : 0u);
    combine(config.enable_nested_parallel_frontier ? 1u : 0u);
    combine(config.enable_nested_frontier_deferral ? 1u : 0u);
    combine(config.enable_nested_frontier_promotion ? 1u : 0u);
    combine(config.enable_frontier_descendant_direct_mode ? 1u : 0u);
    combine(config.enable_session_local_plan_memo ? 1u : 0u);
    combine(config.nested_min_iterations_per_worker);
    combine(config.nested_min_chunks_per_worker);
    combine(config.nested_target_chunks_per_worker);
    combine_double(config.nested_min_parallel_work_ms);
    combine_double(config.nested_target_chunk_ms);
    combine_double(config.nested_plan_hysteresis);
    combine_double(config.parallel_for_minimum_predicted_speedup);
    combine_double(config.parallel_for_imbalance_penalty);
    combine_double(config.parallel_for_estimated_overhead_ms);
    combine_double(config.parallel_for_sequential_fast_path_speedup_margin);
    combine(config.parallel_for_policy_generation);
    combine(config.enable_parallel_for_auto_profiling ? 1u : 0u);
    combine(config.enable_nested_online_telemetry ? 1u : 0u);
    combine(config.enable_nested_root_online_telemetry ? 1u : 0u);
    combine(config.enable_root_analytical_cold_start ? 1u : 0u);
    combine(config.root_analytical_cold_min_iterations_per_worker);
    combine(config.enable_root_pilot_cold_start ? 1u : 0u);
    combine_double(config.root_pilot_cold_min_estimated_work_ms);
    combine(config.enable_parallel_for_backend_calibration ? 1u : 0u);
    combine(config.enable_parallel_for_tiny_work_bypass ? 1u : 0u);
    combine_double(config.parallel_for_tiny_work_bypass_max_ms);
    combine(config.parallel_for_tiny_work_bypass_min_observations);
    combine(config.parallel_for_backend_calibration_min_samples);
    combine_double(config.parallel_for_backend_calibration_hysteresis_percent);
    combine(config.enable_adaptive_execution_candidates ? 1u : 0u);
    combine(config.enable_chunk_neighborhood_candidates ? 1u : 0u);
    combine(config.enable_static_thread_auto_candidates ? 1u : 0u);
    combine(config.minimum_adaptive_workers);
    combine(config.minimum_dynamic_chunk_size);
    combine(config.maximum_dynamic_chunk_size);
    combine_double(config.target_dynamic_chunk_ms);
    combine(config.enable_machine_runtime_calibration ? 1u : 0u);
    combine(config.enable_utility_model_runtime ? 1u : 0u);
    combine(config.enable_experience_ranking ? 1u : 0u);
    return hash;
}

inline std::vector<std::size_t> parallel_for_sample_indices(std::size_t total)
{
    const auto& config = effective_config();
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
    profile.estimated_parallel_overhead_ms = effective_config().parallel_for_estimated_overhead_ms;
    const double parallel_ms = profile.estimated_parallel_overhead_ms
                               + (profile.estimated_total_work_ms / static_cast<double>(workers))
                                     * effective_config().parallel_for_imbalance_penalty;
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
        measured_ms >= effective_config().parallel_for_profile_min_signal_ms;

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
        std::min(effective_config().parallel_for_profile_min_signal_ms,
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

inline std::size_t combine_hash(std::size_t seed, std::size_t value) noexcept
{
    return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
}

class ParentNestedTelemetryScope
{
  public:
    explicit ParentNestedTelemetryScope(const ExecutionContext& parent)
        : telemetry_(parent.depth > 0 ? parent.telemetry : nullptr),
          start_(std::chrono::steady_clock::now())
    {
    }

    ~ParentNestedTelemetryScope()
    {
        if (!telemetry_)
            return;
        telemetry_->nested_call_count.fetch_add(1, std::memory_order_relaxed);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start_);
        telemetry_->nested_elapsed_ns.fetch_add(
            static_cast<std::uint64_t>(elapsed.count()), std::memory_order_relaxed);
    }

  private:
    std::shared_ptr<LoopTelemetryState> telemetry_;
    std::chrono::steady_clock::time_point start_;
};

inline double telemetry_nested_ms(const std::shared_ptr<LoopTelemetryState>& telemetry) noexcept
{
    return telemetry == nullptr
        ? 0.0
        : static_cast<double>(telemetry->nested_elapsed_ns.load(std::memory_order_relaxed)) / 1.0e6;
}
} // namespace detail

template <typename Function>
void parallel_for(std::size_t begin, std::size_t end, Function func)
{
    std::unique_ptr<detail::ExecutionContextScope> default_runtime_scope;
    ExecutionContext default_context;
    if (detail::active_execution_context() == nullptr)
    {
        default_context = default_execution_context();
        default_runtime_scope = std::make_unique<detail::ExecutionContextScope>(default_context);
    }

    if (end < begin)
        throw std::invalid_argument("SmartParallel parallel_for end must not precede begin");

    const std::size_t total = end - begin;
    const ExecutionContext* parent_context_pointer = detail::active_execution_context();
    if (total == 0)
        return;

    // v1.7 exact-plan path. Semantic Deterministic replay and a first
    // Adaptive warm-start call install a fully validated plan before entering
    // this function. This branch intentionally bypasses profiling, cache
    // lookup, learning, exploration, holdout, drift detection, and route
    // switching. The override is suspended while user callbacks execute so
    // nested user loops are never accidentally forced onto the parent plan.
    if (const ExecutionPlan* exact_plan = detail::active_forced_execution_plan())
    {
        ExecutionPlan plan = *exact_plan;
        if (plan.parallel)
        {
            if (plan.engine == ExecutionEngineType::Auto
                || !execution_backend_available(plan.engine))
                throw std::runtime_error(
                    "SmartParallel exact execution plan requires an available scheduler");
            if (plan.job_count == 0)
                throw std::runtime_error(
                    "SmartParallel exact execution plan requires a positive worker count");
        }
        else
        {
            plan.strategy = ExecutionStrategy::Sequential;
            plan.job_count = 1;
            plan.chunk_size = std::max<std::size_t>(std::size_t{1}, plan.chunk_size);
        }

        global_last_parallel_for_profile_diagnostics() = ParallelForProfileDiagnostics{};
        global_last_parallel_for_nested_diagnostics() = ParallelForNestedDiagnostics{};
        ExecutionContext execution_context = detail::make_execution_context();
        execution_context.telemetry = std::make_shared<LoopTelemetryState>();
        execution_context.engine = plan.parallel ? plan.engine : ExecutionEngineType::Auto;
        execution_context.parallel = plan.parallel;
        execution_context.nested_policy = NestedExecutionPolicy::NotNested;
        execution_context.concurrency_budget = plan.parallel
            ? std::max<std::size_t>(std::size_t{1}, plan.job_count)
            : std::size_t{1};
        const ExecutionContext parent_execution_context = parent_context_pointer == nullptr
            ? ExecutionContext{}
            : *parent_context_pointer;
        inherit_execution_lineage(execution_context, parent_execution_context);

        DecisionReport report{};
        report.plan = plan;
        global_last_decision_report() = report;

        Workload workload = WorkloadBuilder::index_range(total);
        detail::ForcedExecutionPlanSuspendScope suspend_override;
        detail::ExecutionContextScope context_scope(execution_context);
        const ExecutionStats stats = execute_workload(
            workload,
            plan,
            [&](std::size_t index) { func(begin + index); },
            NestedExecutionPolicy::NotNested,
            &execution_context);
        auto& diagnostics = global_last_parallel_for_profile_diagnostics();
        diagnostics.profile_available = true;
        diagnostics.execution_ms = stats.elapsed_ms;
        diagnostics.total_ms = stats.elapsed_ms;
        return;
    }

    // A sealed frontier is an immutable root-session decision: descendants are
    // sequential until the frontier returns. In normal execution, preserve the
    // existing parent context and run directly before constructing callsite
    // identity, telemetry, lineage, cache keys, or decision state. Grandchildren
    // observe the same sealed parent and take the same path.
    const bool sealed_descendant_direct =
        effective_config().enable_frontier_descendant_direct_mode
        && parent_context_pointer != nullptr
        && parent_context_pointer->nested_session != nullptr
        && parent_context_pointer->frontier_descendants_sealed
        && !parent_context_pointer->conservative_nested_learning
        && !effective_config().enable_nested_execution_trace;
    if (sealed_descendant_direct)
    {
        if (parent_context_pointer->collect_nested_telemetry
            && parent_context_pointer->telemetry)
        {
            parent_context_pointer->telemetry->nested_call_count.fetch_add(
                1, std::memory_order_relaxed);
        }
        detail::execute_parallel_for_sequential_range(func, begin, end);
        return;
    }

    global_last_parallel_for_profile_diagnostics() = ParallelForProfileDiagnostics{};
    global_last_parallel_for_nested_diagnostics() = ParallelForNestedDiagnostics{};
    const ExecutionContext parent_execution_context = parent_context_pointer == nullptr
        ? ExecutionContext{}
        : *parent_context_pointer;
    const std::size_t function_hash = detail::callable_identity_hash(func);
    detail::ParentNestedTelemetryScope parent_telemetry_scope(parent_execution_context);

    // Once an ancestor owns the parallel frontier, descendants are a detailed
    // sequential call path when tracing or conservative learning requires full
    // lineage. Normal execution has already returned through the sealed path.
    const bool inherited_parallel_frontier =
        parent_execution_context.nested_session != nullptr
        && (parent_execution_context.conservative_nested_learning
            || parent_execution_context.frontier_descendants_sealed
            || (effective_config().enable_nested_parallel_frontier
                && (parent_execution_context.parallel
                    || parent_execution_context.nearest_parallel_ancestor_loop_id != 0)));
    if (inherited_parallel_frontier)
    {
        ExecutionContext execution_context = detail::make_execution_context();
        execution_context.callsite_hash =
            detail::combine_hash(function_hash, iteration_bucket(total));
        execution_context.telemetry = std::make_shared<LoopTelemetryState>();
        execution_context.engine = ExecutionEngineType::Auto;
        execution_context.parallel = false;
        execution_context.nested_policy = NestedExecutionPolicy::SequentialFallback;
        execution_context.concurrency_budget = 1;
        inherit_execution_lineage(execution_context, parent_execution_context);

        if (effective_config().enable_timing_diagnostics)
            clear_timing_report();
        TimingScope total_scope("total");
        Timer whole_call_timer;

        if (execution_context.nested_session)
        {
            NestedExecutionTraceRecord trace;
            trace.root_loop_id = execution_context.root_loop_id;
            trace.loop_id = execution_context.loop_id;
            trace.parent_loop_id = execution_context.parent_loop_id;
            trace.callsite_hash = execution_context.callsite_hash;
            trace.parent_callsite_hash = execution_context.parent_callsite_hash;
            trace.depth = execution_context.depth;
            trace.iterations = total;
            trace.requested_backend = "auto";
            trace.backend = "sequential";
            trace.backend_confirmed = true;
            trace.runtime_concurrency = 1;
            trace.policy = "sequential_fallback";
            trace.mechanism = "sequential_fallback";
            trace.decision_reason = parent_execution_context.conservative_nested_learning
                ? "conservative_learning_descendant"
                : "frontier_descendant_fast_path";
            trace.parallel = false;
            trace.requested_budget = 1;
            trace.effective_budget = 1;
            execution_context.nested_session->begin_trace(std::move(trace));
        }

        DecisionReport report{};
        report.plan.parallel = false;
        report.plan.strategy = ExecutionStrategy::Sequential;
        report.plan.job_count = 1;
        global_last_decision_report() = report;

        Timer execution_timer;
        try
        {
            detail::ExecutionContextScope context_scope(execution_context);
            detail::execute_parallel_for_sequential_range(func, begin, end);
        }
        catch (...)
        {
            const double total_ms = whole_call_timer.elapsed_ms();
            const std::size_t child_calls =
                execution_context.telemetry->nested_call_count.load(std::memory_order_relaxed);
            const double child_ms = detail::telemetry_nested_ms(execution_context.telemetry);
            if (execution_context.nested_session)
                execution_context.nested_session->abort_trace(
                    execution_context.loop_id, total_ms, child_calls, child_ms,
                    "descendant_exception");
            throw;
        }

        auto& diagnostics = global_last_parallel_for_profile_diagnostics();
        diagnostics.sequential_fast_path = true;
        diagnostics.execution_ms = execution_timer.elapsed_ms();
        diagnostics.total_ms = whole_call_timer.elapsed_ms();
        const std::size_t child_calls =
            execution_context.telemetry->nested_call_count.load(std::memory_order_relaxed);
        const double child_ms = detail::telemetry_nested_ms(execution_context.telemetry);

        if (parent_execution_context.conservative_nested_learning
            && effective_config().enable_parallel_for_profile_cache)
        {
            FunctionProfile learned_profile =
                detail::make_parallel_for_profile(total, total, diagnostics.execution_ms);
            const std::size_t inherited_budget = execution_context.nested_session
                ? execution_context.nested_session->total_budget()
                : std::max<std::size_t>(
                      1, parent_execution_context.inherited_concurrency_budget);
            const FunctionProfileKey learned_key{
                function_hash,
                sizeof(std::size_t),
                iteration_bucket(total),
                execution_context.depth,
                execution_context.parent_callsite_hash,
                inherited_budget,
                effective_config().execution_engine,
                detail::parallel_policy_signature(effective_config())};
            const std::uint64_t learned_cache_epoch =
                global_function_profile_cache().cache_epoch();
            global_function_profile_cache().store(
                learned_key,
                learned_profile,
                effective_config().parallel_for_profile_cache_blend,
                effective_config().parallel_for_minimum_predicted_speedup,
                effective_config().parallel_for_sequential_fast_path_min_observations,
                child_calls,
                child_ms,
                execution_context.root_loop_id,
                learned_cache_epoch);
            diagnostics.profile_available = true;
        }
        if (execution_context.nested_session)
            execution_context.nested_session->finish_trace(
                execution_context.loop_id, diagnostics.total_ms, child_calls, child_ms);
        return;
    }

    ExecutionContext execution_context = detail::make_execution_context();
    execution_context.callsite_hash = detail::combine_hash(function_hash, iteration_bucket(total));
    execution_context.telemetry = std::make_shared<LoopTelemetryState>();

    if (effective_config().enable_nested_execution_session
        && parent_execution_context.nested_session == nullptr)
    {
        const std::size_t configured_budget = effective_config().nested_root_concurrency_budget;
        const std::size_t root_budget = configured_budget == 0
            ? std::max<std::size_t>(1, hardware_characteristics().logical_threads)
            : configured_budget;
        execution_context.nested_session =
            std::make_shared<NestedExecutionSession>(root_budget, execution_context.loop_id);
    }

    // Profiling and cold telemetry execute callbacks exactly once in a
    // sequential context while retaining the root session and lineage.
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

    if (effective_config().enable_timing_diagnostics)
        clear_timing_report();

    TimingScope total_scope("total");
    Timer whole_call_timer;
    FunctionProfile function_profile;
    std::vector<std::size_t> sampled_indices;

    const std::size_t inherited_budget = execution_context.nested_session
        ? execution_context.nested_session->total_budget()
        : std::max<std::size_t>(1, parent_execution_context.inherited_concurrency_budget);
    const ExecutionEngineType context_engine = effective_config().execution_engine;
    const std::size_t policy_signature = detail::parallel_policy_signature(effective_config());
    const FunctionProfileKey cache_key{
        function_hash,
        sizeof(std::size_t),
        iteration_bucket(total),
        execution_context.depth,
        execution_context.parent_callsite_hash,
        inherited_budget,
        context_engine,
        policy_signature};
    const NestedPlanSnapshotKey plan_snapshot_key{
        cache_key.function_hash,
        cache_key.element_size,
        cache_key.iteration_bucket,
        cache_key.depth,
        cache_key.parent_callsite_hash,
        cache_key.concurrency_budget,
        cache_key.engine,
        cache_key.policy_signature,
        total};

    const std::optional<ExecutionPlan> session_plan_memo =
        effective_config().enable_session_local_plan_memo && execution_context.nested_session
        ? execution_context.nested_session->find_plan_snapshot(plan_snapshot_key)
        : std::optional<ExecutionPlan>{};

    std::optional<CachedFunctionProfile> cached_profile;
    const std::uint64_t profile_cache_epoch = global_function_profile_cache().cache_epoch();
    std::uint64_t profile_generation = 0;
    bool cache_revalidation_due = false;
    FunctionProfileCache::RevalidationGuard cache_revalidation_guard;
    if (!session_plan_memo && effective_config().enable_parallel_for_profile_cache)
    {
        Timer cache_lookup_timer;
        cached_profile = global_function_profile_cache().find(cache_key);
        if (cached_profile)
            profile_generation = cached_profile->generation;
        global_last_parallel_for_profile_diagnostics().cache_lookup_ms =
            cache_lookup_timer.elapsed_ms();
        if (cached_profile
            && cached_profile->hits >= effective_config().parallel_for_profile_cache_min_hits)
        {
            const auto& config = effective_config();
            const bool cached_predicts_sequential =
                cached_profile->profile.parallel_worthiness
                < config.parallel_for_minimum_predicted_speedup;
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
            const bool stable_plan_revalidation =
                cached_profile->stable_plan_available
                && config.parallel_for_stable_plan_revalidate_interval > 0
                && cached_profile->stable_plan_uses
                       >= config.parallel_for_stable_plan_revalidate_interval;
            const bool age_revalidation =
                config.parallel_for_profile_revalidate_after_ms > 0
                && cached_profile->last_profile_update.time_since_epoch().count() != 0
                && std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - cached_profile->last_profile_update)
                       .count()
                       >= static_cast<long long>(config.parallel_for_profile_revalidate_after_ms);
            cache_revalidation_due = needs_confirmation || periodic_revalidation
                || stable_plan_revalidation || age_revalidation;

            if (cache_revalidation_due)
            {
                cache_revalidation_guard =
                    global_function_profile_cache().try_acquire_revalidation(cache_key);
                // Revalidation is single-flight. Concurrent roots continue with
                // the last complete profile/plan instead of all switching to a
                // conservative learning pass at once.
                if (!cache_revalidation_guard.owns_revalidation())
                    cache_revalidation_due = false;
            }

            if (!cache_revalidation_due)
            {
                function_profile = cached_profile->profile;
                detail::update_parallel_cost_model(function_profile, total);
                global_last_parallel_for_profile_diagnostics().cache_hit = true;
            }
        }
    }

    const bool stable_cached_plan_hit =
        !session_plan_memo && function_profile.available && cached_profile
        && cached_profile->stable_plan_available && !cache_revalidation_due;

    const bool nested_public_call = parent_execution_context.depth > 0;
    const bool use_online_nested_telemetry =
        nested_public_call && effective_config().enable_nested_online_telemetry;
    const bool use_online_root_telemetry =
        !nested_public_call && execution_context.nested_session != nullptr
        && effective_config().enable_nested_root_online_telemetry
        && effective_config().enable_parallel_for_profile_cache;
    const bool use_online_execution_telemetry =
        !session_plan_memo && (use_online_nested_telemetry || use_online_root_telemetry);
    FunctionProfileCache::ProfileBuildGuard nested_profile_build_guard;
    bool nested_profile_store_allowed = true;
    if (!function_profile.available && use_online_execution_telemetry
        && effective_config().enable_parallel_for_profile_cache && !cached_profile)
    {
        nested_profile_build_guard =
            global_function_profile_cache().try_acquire_profile_build(cache_key);
        nested_profile_store_allowed = nested_profile_build_guard.owns_build();
    }

    // Cold calls are learned from the real exactly-once execution. Large roots
    // use a conservative analytical parallel plan. Coarse roots with only one
    // item per worker use one in-band pilot item: the callback is executed once,
    // and only the remaining items are promoted when that observation predicts
    // enough useful work. Nested cold calls remain sequential so frontier
    // discovery and exactly-once semantics are preserved.
    if (!function_profile.available && use_online_execution_telemetry)
    {
        const std::size_t cold_parallel_threshold = inherited_budget
            * std::max<std::size_t>(
                1, effective_config().root_analytical_cold_min_iterations_per_worker);
        const bool analytical_parallel_cold =
            use_online_root_telemetry
            && effective_config().enable_root_analytical_cold_start
            && inherited_budget > 1
            && total >= cold_parallel_threshold;
        const bool pilot_cold_candidate =
            use_online_root_telemetry
            && effective_config().enable_root_pilot_cold_start
            && inherited_budget > 1
            && total >= inherited_budget
            && total < cold_parallel_threshold;

        ExecutionPlan cold_plan;
        cold_plan.parallel = analytical_parallel_cold;
        cold_plan.engine = effective_config().execution_engine == ExecutionEngineType::Auto
            ? ExecutionEngineType::ThreadPool
            : effective_config().execution_engine;
        cold_plan.strategy = analytical_parallel_cold
            ? (cold_plan.engine == ExecutionEngineType::StaticThread
                   ? ExecutionStrategy::StaticChunks
                   : ExecutionStrategy::DynamicChunks)
            : ExecutionStrategy::Sequential;
        cold_plan.job_count = analytical_parallel_cold
            ? std::max<std::size_t>(1, std::min(inherited_budget, total))
            : 1;
        if (analytical_parallel_cold && cold_plan.strategy == ExecutionStrategy::DynamicChunks)
        {
            const std::size_t target_chunks = std::max<std::size_t>(
                cold_plan.job_count,
                cold_plan.job_count * std::max<std::size_t>(
                    1, effective_config().nested_target_chunks_per_worker));
            cold_plan.chunk_size = std::max<std::size_t>(
                1, (total + target_chunks - 1) / target_chunks);
        }

        execution_context.conservative_nested_learning = !analytical_parallel_cold;
        execution_context.engine = analytical_parallel_cold
            ? resolve_execution_engine_type(cold_plan.engine)
            : ExecutionEngineType::Auto;
        execution_context.parallel = analytical_parallel_cold;
        execution_context.nested_policy = NestedExecutionPolicy::NotNested;
        execution_context.concurrency_budget = cold_plan.job_count;
        execution_context.frontier_descendants_sealed =
            analytical_parallel_cold && effective_config().enable_nested_parallel_frontier;
        execution_context.collect_nested_telemetry = true;
        inherit_execution_lineage(execution_context, parent_execution_context);

        const auto begin_cold_trace = [&](const char* mechanism,
                                          const char* learned_reason)
        {
            if (!execution_context.nested_session)
                return;
            NestedExecutionTraceRecord trace;
            trace.root_loop_id = execution_context.root_loop_id;
            trace.loop_id = execution_context.loop_id;
            trace.parent_loop_id = execution_context.parent_loop_id;
            trace.callsite_hash = execution_context.callsite_hash;
            trace.parent_callsite_hash = execution_context.parent_callsite_hash;
            trace.depth = execution_context.depth;
            trace.iterations = total;
            trace.requested_backend = cold_plan.parallel
                ? runtime_name(cold_plan.engine)
                : "auto";
            trace.backend = cold_plan.parallel ? "unconfirmed" : "sequential";
            trace.backend_confirmed = !cold_plan.parallel;
            trace.runtime_concurrency = cold_plan.parallel ? 0 : 1;
            trace.policy = "not_nested";
            trace.mechanism = mechanism;
            trace.decision_reason = cache_revalidation_due
                ? (use_online_root_telemetry ? "root_online_revalidation"
                                             : "nested_online_revalidation")
                : (nested_profile_store_allowed
                       ? learned_reason
                       : (use_online_root_telemetry ? "root_online_shared_cold_execution"
                                                    : "nested_online_shared_cold_execution"));
            trace.parallel = cold_plan.parallel;
            trace.requested_budget = inherited_budget;
            trace.effective_budget = cold_plan.job_count;
            trace.chunk_size = cold_plan.chunk_size;
            execution_context.nested_session->begin_trace(std::move(trace));
        };

        Timer execution_timer;
        double pilot_ms = 0.0;
        double pilot_estimated_sequential_ms = 0.0;
        bool pilot_promoted = false;
        try
        {
            if (analytical_parallel_cold)
            {
                begin_cold_trace("analytical_cold_execution",
                                 "root_analytical_cold_learning");
                const Workload cold_workload = WorkloadBuilder::index_range(total);
                detail::ExecutionContextScope backend_scope(execution_context);
                execute_workload(
                    cold_workload,
                    cold_plan,
                    [&](std::size_t index) { invoke(begin + index); },
                    NestedExecutionPolicy::NotNested,
                    &execution_context);
            }
            else if (pilot_cold_candidate)
            {
                Timer pilot_timer;
                invoke(begin);
                pilot_ms = pilot_timer.elapsed_ms();
                pilot_estimated_sequential_ms = pilot_ms * static_cast<double>(total);
                const std::size_t remaining = total - 1;
                pilot_promoted = remaining > 1
                    && pilot_estimated_sequential_ms
                           >= std::max(0.0,
                               effective_config().root_pilot_cold_min_estimated_work_ms);

                if (pilot_promoted)
                {
                    cold_plan.parallel = true;
                    cold_plan.strategy = cold_plan.engine == ExecutionEngineType::StaticThread
                        ? ExecutionStrategy::StaticChunks
                        : ExecutionStrategy::DynamicChunks;
                    cold_plan.job_count = std::max<std::size_t>(
                        1, std::min(inherited_budget, remaining));
                    if (cold_plan.strategy == ExecutionStrategy::DynamicChunks)
                    {
                        const std::size_t target_chunks = std::max<std::size_t>(
                            cold_plan.job_count,
                            cold_plan.job_count * std::max<std::size_t>(
                                1, effective_config().nested_target_chunks_per_worker));
                        cold_plan.chunk_size = std::max<std::size_t>(
                            1, (remaining + target_chunks - 1) / target_chunks);
                    }
                    execution_context.conservative_nested_learning = false;
                    execution_context.engine = resolve_execution_engine_type(cold_plan.engine);
                    execution_context.parallel = true;
                    execution_context.concurrency_budget = cold_plan.job_count;
                    execution_context.frontier_descendants_sealed =
                        effective_config().enable_nested_parallel_frontier;
                    begin_cold_trace("pilot_cold_execution",
                                     "root_pilot_cold_learning");
                    const Workload remaining_workload =
                        WorkloadBuilder::index_range(remaining);
                    detail::ExecutionContextScope backend_scope(execution_context);
                    execute_workload(
                        remaining_workload,
                        cold_plan,
                        [&](std::size_t index) { invoke(begin + 1 + index); },
                        NestedExecutionPolicy::NotNested,
                        &execution_context);
                }
                else
                {
                    begin_cold_trace("pilot_sequential_execution",
                                     "root_pilot_sequential_learning");
                    detail::ExecutionContextScope context_scope(execution_context);
                    detail::execute_parallel_for_sequential_range(func, begin + 1, end);
                }
            }
            else
            {
                begin_cold_trace("direct_execution",
                                 use_online_root_telemetry
                                     ? "root_online_cold_learning"
                                     : "nested_online_cold_learning");
                detail::ExecutionContextScope context_scope(execution_context);
                detail::execute_parallel_for_sequential_range(func, begin, end);
            }
        }
        catch (...)
        {
            const double total_ms = whole_call_timer.elapsed_ms();
            const std::size_t failed_child_calls =
                execution_context.telemetry->nested_call_count.load(std::memory_order_relaxed);
            const double failed_child_ms = detail::telemetry_nested_ms(execution_context.telemetry);
            if (execution_context.nested_session)
                execution_context.nested_session->abort_trace(
                    execution_context.loop_id, total_ms, failed_child_calls, failed_child_ms,
                    "online_learning_exception");
            throw;
        }
        const double execution_ms = execution_timer.elapsed_ms();
        const std::size_t child_calls =
            execution_context.telemetry->nested_call_count.load(std::memory_order_relaxed);
        const double child_ms = detail::telemetry_nested_ms(execution_context.telemetry);
        function_profile = detail::make_parallel_for_profile(total, total, execution_ms);
        if ((analytical_parallel_cold || pilot_promoted) && function_profile.available)
        {
            const double estimated_sequential_ms = pilot_promoted
                ? std::max(pilot_estimated_sequential_ms, execution_ms)
                : execution_ms * static_cast<double>(cold_plan.job_count)
                    / std::max(1.0, effective_config().parallel_for_imbalance_penalty);
            function_profile.avg_ms_per_iteration = estimated_sequential_ms
                / static_cast<double>(std::max<std::size_t>(1, total));
            function_profile.median_ms_per_iteration = function_profile.avg_ms_per_iteration;
            function_profile.trimmed_mean_ms_per_iteration = function_profile.avg_ms_per_iteration;
            function_profile.p95_ms_per_iteration = function_profile.avg_ms_per_iteration;
            function_profile.max_ms_per_iteration = function_profile.avg_ms_per_iteration;
            function_profile.metadata.confidence = ObservationConfidence::Low;
            function_profile.measurement_reliable = false;
            detail::update_parallel_cost_model(function_profile, total);
        }

        if (effective_config().enable_parallel_for_profile_cache
            && nested_profile_store_allowed)
        {
            profile_generation = global_function_profile_cache().store(
                cache_key,
                function_profile,
                effective_config().parallel_for_profile_cache_blend,
                effective_config().parallel_for_minimum_predicted_speedup,
                effective_config().parallel_for_sequential_fast_path_min_observations,
                child_calls,
                child_ms,
                execution_context.root_loop_id,
                profile_cache_epoch);
        }

        auto& diagnostics = global_last_parallel_for_profile_diagnostics();
        diagnostics.profile_available = function_profile.available;
        diagnostics.sampled_iterations = total;
        diagnostics.profiling_ms = execution_ms;
        diagnostics.execution_ms = execution_ms;
        diagnostics.total_ms = whole_call_timer.elapsed_ms();

        DecisionReport report{};
        report.has_function_profile = true;
        report.function_profile = function_profile;
        report.plan = cold_plan;
        global_last_decision_report() = report;

        if (execution_context.nested_session)
            execution_context.nested_session->finish_trace(
                execution_context.loop_id, diagnostics.total_ms, child_calls, child_ms);
        return;
    }

    // Preserve the mature sequential fast path for leaf callbacks. Nested
    // callsites remain eligible for frontier promotion instead of becoming a
    // sticky local sequential choice.
    const auto& profile_diagnostics = global_last_parallel_for_profile_diagnostics();
    const bool cached_cost_evidence =
        profile_diagnostics.cache_hit && cached_profile && function_profile.samples > 0;
    const double fast_path_threshold =
        effective_config().parallel_for_minimum_predicted_speedup
        * effective_config().parallel_for_sequential_fast_path_speedup_margin;
    const bool high_confidence =
        cached_profile
        && cached_profile->observations
            >= effective_config().parallel_for_sequential_fast_path_min_observations
        && function_profile.stable
        && function_profile.metadata.confidence == ObservationConfidence::High;
    const bool nested_frontier_candidate =
        cached_profile && cached_profile->observed_nested_calls
        && execution_context.nested_session != nullptr;
    const bool tiny_work_confidence =
        cached_profile
        && cached_profile->observations >= std::max<std::size_t>(
               effective_config().parallel_for_sequential_fast_path_min_observations,
               effective_config().parallel_for_tiny_work_bypass_min_observations)
        && function_profile.stable;
    const bool tiny_descendant_under_sealed_frontier =
        nested_public_call && parent_execution_context.frontier_descendants_sealed;
    const bool tiny_root_has_sequential_profitability_evidence =
        !nested_public_call
        && total == 1
        && nested_frontier_candidate
        && function_profile.parallel_worthiness < fast_path_threshold;
    const bool tiny_work_absolute_bypass =
        cached_cost_evidence
        && tiny_work_confidence
        && effective_config().execution_engine == ExecutionEngineType::Auto
        && effective_config().enable_parallel_for_tiny_work_bypass
        && effective_config().parallel_for_tiny_work_bypass_max_ms > 0.0
        && function_profile.estimated_total_work_ms > 0.0
        && function_profile.estimated_total_work_ms
               <= effective_config().parallel_for_tiny_work_bypass_max_ms
        && (tiny_descendant_under_sealed_frontier
            || tiny_root_has_sequential_profitability_evidence);
    const bool profitability_sequential_bypass =
        cached_cost_evidence && high_confidence
        && !nested_frontier_candidate
        && effective_config().enable_parallel_for_cached_sequential_fast_path
        && function_profile.parallel_worthiness < fast_path_threshold;
    if (tiny_work_absolute_bypass || profitability_sequential_bypass)
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
        execution_context.frontier_descendants_sealed =
            tiny_work_absolute_bypass
            && effective_config().enable_nested_parallel_frontier;
        inherit_execution_lineage(execution_context, parent_execution_context);

        if (execution_context.nested_session)
        {
            NestedExecutionTraceRecord trace;
            trace.root_loop_id = execution_context.root_loop_id;
            trace.loop_id = execution_context.loop_id;
            trace.parent_loop_id = execution_context.parent_loop_id;
            trace.callsite_hash = execution_context.callsite_hash;
            trace.parent_callsite_hash = execution_context.parent_callsite_hash;
            trace.depth = execution_context.depth;
            trace.iterations = total;
            trace.requested_backend = "auto";
            trace.backend = "sequential";
            trace.backend_confirmed = true;
            trace.runtime_concurrency = 1;
            trace.policy = "not_nested";
            trace.mechanism = "direct_execution";
            trace.decision_reason = tiny_work_absolute_bypass
                ? "tiny_work_absolute_bypass"
                : "cached_sequential_fast_path";
            trace.cache_hit = true;
            trace.profile_available = true;
            trace.parallel = false;
            trace.estimated_work_ms = function_profile.estimated_total_work_ms;
            execution_context.nested_session->begin_trace(std::move(trace));
        }

        Timer execution_timer;
        try
        {
            detail::ExecutionContextScope context_scope(execution_context);
            detail::execute_parallel_for_sequential_range(func, begin, end);
        }
        catch (...)
        {
            const double total_ms = whole_call_timer.elapsed_ms();
            const std::size_t failed_child_calls =
                execution_context.telemetry->nested_call_count.load(std::memory_order_relaxed);
            const double failed_child_ms = detail::telemetry_nested_ms(execution_context.telemetry);
            if (execution_context.nested_session)
                execution_context.nested_session->abort_trace(
                    execution_context.loop_id, total_ms, failed_child_calls, failed_child_ms,
                    "sequential_fast_path_exception");
            throw;
        }
        diagnostics.execution_ms = execution_timer.elapsed_ms();
        diagnostics.total_ms = whole_call_timer.elapsed_ms();
        const std::size_t child_calls =
            execution_context.telemetry->nested_call_count.load(std::memory_order_relaxed);
        const double child_ms = detail::telemetry_nested_ms(execution_context.telemetry);
        if (execution_context.nested_session)
            execution_context.nested_session->finish_trace(
                execution_context.loop_id, diagnostics.total_ms, child_calls, child_ms);

        const ExecutionStats stats{total, diagnostics.total_ms};
        if (effective_config().enable_experience)
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

    Workload workload = WorkloadBuilder::index_range(total);
    WorkloadAnalysis analysis;
    if (!stable_cached_plan_hit && !session_plan_memo)
    {
        Timer analysis_timer;
        WorkloadAnalyzer analyzer;
        analysis = analyzer.analyze(workload);
        global_last_parallel_for_profile_diagnostics().workload_analysis_ms =
            analysis_timer.elapsed_ms();
    }

    if (!function_profile.available && effective_config().enable_parallel_for_auto_profiling)
    {
        Timer profiling_timer;
        sampled_indices = detail::parallel_for_sample_indices(total);
        Timer sample_timer;
        std::size_t executed = 0;
        for (const std::size_t index : sampled_indices)
        {
            invoke(begin + index);
            ++executed;
            if (executed >= effective_config().parallel_for_profile_min_samples
                && sample_timer.elapsed_ms() >= effective_config().parallel_for_profile_min_signal_ms)
                break;
        }
        sampled_indices.resize(executed);
        function_profile =
            detail::make_parallel_for_profile(total, executed, sample_timer.elapsed_ms());
        std::sort(sampled_indices.begin(), sampled_indices.end());
        global_last_parallel_for_profile_diagnostics().sampled_iterations = executed;
        global_last_parallel_for_profile_diagnostics().profiling_ms = profiling_timer.elapsed_ms();
        if (effective_config().enable_parallel_for_profile_cache)
        {
            const std::size_t child_calls =
                execution_context.telemetry->nested_call_count.load(std::memory_order_relaxed);
            const double child_ms = detail::telemetry_nested_ms(execution_context.telemetry);
            profile_generation = global_function_profile_cache().store(
                cache_key,
                function_profile,
                effective_config().parallel_for_profile_cache_blend,
                effective_config().parallel_for_minimum_predicted_speedup,
                effective_config().parallel_for_sequential_fast_path_min_observations,
                child_calls,
                child_ms,
                execution_context.root_loop_id,
                profile_cache_epoch);
        }
    }

    Timer decision_timer;
    DecisionEngine engine;
    const FunctionProfile* profile_ptr = function_profile.available ? &function_profile : nullptr;
    ExecutionPlan requested_plan;
    bool plan_snapshot_hit = false;
    if (session_plan_memo)
    {
        requested_plan = *session_plan_memo;
        plan_snapshot_hit = true;
    }
    else if (stable_cached_plan_hit)
    {
        requested_plan = cached_profile->stable_plan;
        plan_snapshot_hit = true;
        global_function_profile_cache().note_stable_plan_use(cache_key);
    }
    if (!plan_snapshot_hit)
        requested_plan = engine.decide(workload, analysis, profile_ptr);

    if (requested_plan.parallel && effective_config().execution_engine != ExecutionEngineType::Auto)
    {
        requested_plan.engine = effective_config().execution_engine;
        requested_plan.strategy = requested_plan.engine == ExecutionEngineType::StaticThread
                                      ? ExecutionStrategy::StaticChunks
                                      : ExecutionStrategy::DynamicChunks;
    }

    std::string decision_reason = session_plan_memo
        ? "session_plan_memo"
        : (stable_cached_plan_hit
               ? "stable_cached_plan"
               : (requested_plan.parallel ? "analytical_parallel" : "analytical_sequential"));
    const bool observed_nested_calls =
        (cached_profile && cached_profile->observed_nested_calls)
        || execution_context.telemetry->nested_call_count.load(std::memory_order_relaxed) > 0;
    const bool has_parallel_ancestor =
        parent_execution_context.parallel
        || parent_execution_context.nearest_parallel_ancestor_loop_id != 0;
    const std::size_t root_budget = execution_context.nested_session
        ? execution_context.nested_session->total_budget()
        : inherited_budget;

    if (effective_config().enable_nested_parallel_frontier
        && execution_context.nested_session)
    {
        if (has_parallel_ancestor)
        {
            requested_plan.parallel = false;
            requested_plan.strategy = ExecutionStrategy::Sequential;
            requested_plan.job_count = 1;
            requested_plan.chunk_size = 0;
            decision_reason = "descendant_of_parallel_frontier";
        }
        else if (observed_nested_calls
                 && effective_config().enable_nested_frontier_deferral
                 && total < root_budget)
        {
            requested_plan.parallel = false;
            requested_plan.strategy = ExecutionStrategy::Sequential;
            requested_plan.job_count = 1;
            requested_plan.chunk_size = 0;
            decision_reason = "defer_underfilled_outer_level";
        }
        else if (observed_nested_calls
                 && effective_config().enable_nested_frontier_promotion
                 && total >= root_budget
                 && function_profile.available
                 && function_profile.estimated_total_work_ms
                        >= effective_config().nested_min_parallel_work_ms
                               * std::max(1.0, effective_config().nested_plan_hysteresis))
        {
            requested_plan.parallel = true;
            requested_plan.engine = effective_config().execution_engine == ExecutionEngineType::Auto
                ? ExecutionEngineType::ThreadPool
                : effective_config().execution_engine;
            requested_plan.strategy = requested_plan.engine == ExecutionEngineType::StaticThread
                ? ExecutionStrategy::StaticChunks
                : ExecutionStrategy::DynamicChunks;
            requested_plan.job_count = std::max<std::size_t>(
                1, std::min(root_budget, total));
            if (requested_plan.strategy == ExecutionStrategy::DynamicChunks
                && function_profile.avg_ms_per_iteration > 0.0)
            {
                requested_plan.chunk_size = std::max<std::size_t>(
                    1, static_cast<std::size_t>(
                           effective_config().nested_target_chunk_ms
                           / function_profile.avg_ms_per_iteration));
            }
            decision_reason = "promote_parallel_frontier";
        }
    }

    if (execution_context.nested_session && requested_plan.parallel)
    {
        requested_plan.job_count = std::max<std::size_t>(
            1, std::min({requested_plan.job_count, root_budget, total}));
    }

    if (effective_config().enable_parallel_for_profile_cache && function_profile.available
        && !stable_cached_plan_hit && profile_generation != 0)
        global_function_profile_cache().store_stable_plan(
            cache_key, requested_plan, profile_generation);

    const bool backend_calibration_eligible =
        parent_execution_context.depth == 0
        && effective_config().execution_engine == ExecutionEngineType::Auto
        && requested_plan.parallel
        && function_profile.available
        && profile_generation != 0;
    if (backend_calibration_eligible)
    {
        const ExecutionEngineType current_backend =
            resolve_execution_engine_type(requested_plan.engine);
        const ExecutionEngineType calibrated_backend =
            global_backend_calibration_cache().select(
                cache_key, profile_generation, current_backend);
        if (calibrated_backend != current_backend)
        {
            requested_plan.engine = calibrated_backend;
            requested_plan.strategy = calibrated_backend == ExecutionEngineType::StaticThread
                ? ExecutionStrategy::StaticChunks
                : ExecutionStrategy::DynamicChunks;
            decision_reason += ";backend_calibration_probe";
        }
        else if (effective_config().enable_parallel_for_backend_calibration)
        {
            decision_reason += ";backend_calibration_reuse";
        }
    }

    NestedExecutionDecision nested_decision =
        NestedExecutionCoordinator{}.coordinate(parent_execution_context, requested_plan);
    if (!session_plan_memo && effective_config().enable_nested_granularity_enforcement
        && nested_decision.plan.parallel)
    {
        NestedExecutionConstraints constraints;
        constraints.iteration_count = total - sampled_indices.size();
        if (constraints.iteration_count == 0)
            constraints.iteration_count = total;
        constraints.minimum_iterations_per_worker = function_profile.available
            ? 1
            : effective_config().nested_min_iterations_per_worker;
        constraints.minimum_chunks_per_worker =
            effective_config().nested_min_chunks_per_worker;
        constraints.target_chunks_per_worker =
            effective_config().nested_target_chunks_per_worker;
        constraints.estimated_total_work_ms = function_profile.available
            ? function_profile.estimated_total_work_ms
            : 0.0;
        constraints.estimated_ms_per_iteration = function_profile.available
            ? function_profile.avg_ms_per_iteration
            : 0.0;
        constraints.minimum_parallel_work_ms =
            effective_config().nested_min_parallel_work_ms;
        constraints.target_chunk_ms = effective_config().nested_target_chunk_ms;
        nested_decision =
            NestedExecutionCoordinator{}.enforce_constraints(nested_decision, constraints);
        if (!nested_decision.plan.parallel && requested_plan.parallel)
            decision_reason = "time_or_granularity_fallback";
    }

    ExecutionPlan plan = nested_decision.plan;
    if (execution_context.nested_session && effective_config().enable_session_local_plan_memo
        && !plan_snapshot_hit)
        execution_context.nested_session->store_plan_snapshot(plan_snapshot_key, plan);
    const NestedExecutionPolicy nested_policy = nested_decision.policy;
    execution_context.engine = plan.parallel ? resolve_execution_engine_type(plan.engine)
                                             : ExecutionEngineType::Auto;
    execution_context.parallel = plan.parallel;
    execution_context.nested_policy = nested_policy;
    execution_context.concurrency_budget = nested_decision.effective_budget;
    execution_context.frontier_descendants_sealed =
        plan.parallel && effective_config().enable_nested_parallel_frontier;
    inherit_execution_lineage(execution_context, parent_execution_context);

    global_last_parallel_for_profile_diagnostics().decision_ms = decision_timer.elapsed_ms();
    DecisionReport decision_report = plan_snapshot_hit ? DecisionReport{} : engine.last_report();
    decision_report.plan = plan;
    if (function_profile.available)
    {
        decision_report.has_function_profile = true;
        decision_report.function_profile = function_profile;
    }
    global_last_decision_report() = decision_report;

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

    if (execution_context.nested_session)
    {
        NestedExecutionTraceRecord trace;
        trace.root_loop_id = execution_context.root_loop_id;
        trace.loop_id = execution_context.loop_id;
        trace.parent_loop_id = execution_context.parent_loop_id;
        trace.callsite_hash = execution_context.callsite_hash;
        trace.parent_callsite_hash = execution_context.parent_callsite_hash;
        trace.depth = execution_context.depth;
        trace.iterations = total;
        trace.requested_backend = runtime_name(plan.parallel ? plan.engine : requested_plan.engine);
        trace.backend = plan.parallel ? "unconfirmed" : "sequential";
        trace.backend_confirmed = !plan.parallel;
        trace.runtime_concurrency = plan.parallel ? 0 : 1;
        trace.policy = nested_execution_policy_name(nested_policy);
        trace.mechanism = nested_execution_mechanism_name(nested_decision.negotiation.mechanism);
        trace.decision_reason = decision_reason;
        trace.cache_hit = global_last_parallel_for_profile_diagnostics().cache_hit;
        trace.profile_available = function_profile.available;
        trace.parallel = plan.parallel;
        trace.plan_snapshot_hit = plan_snapshot_hit;
        trace.estimated_work_ms = function_profile.available
            ? function_profile.estimated_total_work_ms
            : 0.0;
        trace.requested_budget = nested_decision.requested_budget;
        trace.effective_budget = nested_decision.effective_budget;
        trace.chunk_size = plan.chunk_size;
        execution_context.nested_session->begin_trace(std::move(trace));
    }

    Timer execution_timer;
    std::size_t cursor = 0;
    auto execute_gap = [&](std::size_t gap_begin, std::size_t gap_end)
    {
        if (gap_end <= gap_begin)
            return;
        detail::ExecutionContextScope backend_scope(execution_context);
        if (!plan.parallel && plan.strategy == ExecutionStrategy::Sequential
            && detail::HasSmartParallelSequentialRange<Function>::value)
        {
            detail::execute_parallel_for_sequential_range(
                func, begin + gap_begin, begin + gap_end);
            return;
        }

        Workload gap_workload = WorkloadBuilder::index_range(gap_end - gap_begin);
        execute_workload(gap_workload,
                         plan,
                         [&](std::size_t i)
                         {
                             invoke(begin + gap_begin + i);
                         },
                         nested_policy,
                         &execution_context);
    };
    try
    {
        for (const std::size_t sampled_index : sampled_indices)
        {
            execute_gap(cursor, sampled_index);
            cursor = sampled_index + 1;
        }
        execute_gap(cursor, total);
    }
    catch (...)
    {
        const double total_ms = whole_call_timer.elapsed_ms();
        const std::size_t failed_child_calls =
            execution_context.telemetry->nested_call_count.load(std::memory_order_relaxed);
        const double failed_child_ms = detail::telemetry_nested_ms(execution_context.telemetry);
        if (execution_context.nested_session)
            execution_context.nested_session->abort_trace(
                execution_context.loop_id, total_ms, failed_child_calls, failed_child_ms,
                "backend_execution_exception");
        throw;
    }

    auto& final_diagnostics = global_last_parallel_for_profile_diagnostics();
    final_diagnostics.execution_ms = execution_timer.elapsed_ms();
    final_diagnostics.total_ms = whole_call_timer.elapsed_ms();
    const std::size_t child_calls =
        execution_context.telemetry->nested_call_count.load(std::memory_order_relaxed);
    const double child_ms = detail::telemetry_nested_ms(execution_context.telemetry);
    if (execution_context.nested_session)
        execution_context.nested_session->finish_trace(
            execution_context.loop_id, final_diagnostics.total_ms, child_calls, child_ms);

    if (backend_calibration_eligible && plan.parallel)
        global_backend_calibration_cache().record(
            cache_key, profile_generation, execution_context.engine,
            final_diagnostics.execution_ms);

    const ExecutionStats stats{total, final_diagnostics.total_ms};
    if (effective_config().enable_experience)
        record_execution_experience(
            workload, profile_ptr, global_last_decision_report(), plan, stats.elapsed_ms);
}

template <typename Function>
void parallel_for(const ExecutionContext& context,
                  std::size_t begin,
                  std::size_t end,
                  Function func)
{
    const auto exact_plan = detail::generic_context_execution_plan(context);
    detail::ExecutionContextScope context_scope(context);
    detail::ForcedExecutionPlanScope forced_scope(exact_plan ? &*exact_plan : nullptr);
    parallel_for(begin, end, std::move(func));
}

template <std::size_t Dimensions, typename Function>
void parallel_for_nd(const std::array<std::size_t, Dimensions>& extents, Function func)
{
    static_assert(Dimensions > 0, "parallel_for_nd requires at least one dimension");
    std::size_t total = 1;
    for (const std::size_t extent : extents)
    {
        if (extent == 0)
            return;
        if (total > std::numeric_limits<std::size_t>::max() / extent)
            throw std::overflow_error("SmartParallel parallel_for_nd iteration space overflow");
        total *= extent;
    }

    parallel_for(0, total, [&](std::size_t flat_index)
    {
        std::array<std::size_t, Dimensions> indices{};
        std::size_t value = flat_index;
        for (std::size_t dimension = Dimensions; dimension-- > 0;)
        {
            indices[dimension] = value % extents[dimension];
            value /= extents[dimension];
        }

        if constexpr (std::is_invocable_v<Function&, const std::array<std::size_t, Dimensions>&>)
            std::invoke(func, indices);
        else
            std::apply(func, indices);
    });
}

template <typename Container, typename Function>
void for_each(Container& container, Function func)
{
    if (effective_config().enable_timing_diagnostics)
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

        if (effective_config().enable_experience)
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

    if (effective_config().enable_experience)
    {
        TimingScope scope("experience_record");

        record_execution_experience(
            workload, &function_profile, global_last_decision_report(), plan, stats.elapsed_ms);
    }
}

template <typename ContainerA, typename ContainerB, typename Function>
void for_each_pair(ContainerA& a, ContainerB& b, Function func)
{
    if (effective_config().enable_timing_diagnostics)
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

    if (effective_config().enable_experience)
    {
        TimingScope scope("experience_record");

        record_execution_experience(
            workload, &function_profile, global_last_decision_report(), plan, stats.elapsed_ms);
    }
}
} // namespace smart
