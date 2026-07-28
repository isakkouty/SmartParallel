#pragma once

#include <smart/execution/execution_context.hpp>
#include <smart/vision/execution_route.hpp>

#include <cstddef>
#include <vector>

namespace smart::vision
{
struct DecisionReport
{
    ExecutionRoute requested_route = ExecutionRoute::Auto;
    ExecutionRoute selected_route = ExecutionRoute::NativeSequential;
    ExecutionEngineType native_engine = ExecutionEngineType::Auto;

    bool automatic = true;
    bool adaptive_selection_enabled = false;
    bool cache_hit = false;
    bool learned_route = false;
    bool exploration_probe = false;
    bool holdout_probe = false;
    bool revalidation_probe = false;
    bool drift_probe = false;
    bool nested_fallback = false;

    bool opencv_available = false;
    bool backend_authenticated = false;
    bool execution_timed = false;

    std::size_t worker_budget = 1;
    std::size_t participant_count = 1;
    std::size_t chunk_count = 1;
    std::size_t candidate_count = 1;
    std::size_t execution_depth = 0;

    double execution_ms = 0.0;
};

struct RouteTrainingEntry
{
    ExecutionRoute route = ExecutionRoute::NativeSequential;
    double median_ms = 0.0;
    double mad_ms = 0.0;
    double minimum_ms = 0.0;
    double maximum_ms = 0.0;
    std::size_t sample_count = 0;
    std::size_t warmup_count = 0;
    std::size_t holdout_sample_count = 0;
    std::size_t current_sample_count = 0;
    double current_median_ms = 0.0;
    bool active = false;
};

struct RouteTrainingReport
{
    bool available = false;
    bool stable = false;
    bool holdout_active = false;
    bool revalidation_active = false;
    bool drift_detected = false;
    ExecutionRoute stable_route = ExecutionRoute::NativeSequential;
    ExecutionRoute provisional_route = ExecutionRoute::NativeSequential;
    std::size_t verification_failures = 0;
    std::size_t route_switch_count = 0;
    std::size_t drift_strikes = 0;
    ExecutionRoute revalidation_challenger = ExecutionRoute::NativeSequential;
    double training_baseline_ms = 0.0;
    double current_baseline_ms = 0.0;
    double last_revalidation_stable_ms = 0.0;
    double last_revalidation_challenger_ms = 0.0;
    std::vector<RouteTrainingEntry> routes;
};

DecisionReport last_decision_report() noexcept;
RouteTrainingReport last_route_training_report();
} // namespace smart::vision
