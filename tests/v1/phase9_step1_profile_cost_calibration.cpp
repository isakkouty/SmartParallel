#include <cassert>
#include <cmath>

#include <smart/decision/profile_cost_calibration.hpp>

int main()
{
    using namespace smart;

    ProfileCostCalibrationPolicy policy;

    FunctionProfile sparse;
    sparse.available = true;
    sparse.measurement_reliable = false;
    sparse.measured_batches = 2;
    sparse.stop_reason = ProfileStopReason::MeasurementUnreliable;
    sparse.steady_state_ms_per_iteration = 0.001;
    sparse.trimmed_mean_ms_per_iteration = 0.0011;
    sparse.median_ms_per_iteration = 0.001;
    sparse.avg_ms_per_iteration = 0.020; // one or two large outliers
    sparse.coefficient_of_variation = 2.0;
    sparse.tail_ratio = 8.0;
    sparse.regional_cost_ratio = 5.0;

    const auto guarded = policy.evaluate(sparse, 100'000);
    assert(guarded.mean_capped);
    assert(guarded.mean_weight < 0.10);
    assert(guarded.per_iteration_ms < 0.00125);
    assert(guarded.total_work_ms < 125.0);

    FunctionProfile mixed = sparse;
    mixed.measurement_reliable = true;
    mixed.measured_batches = 16;
    mixed.stop_reason = ProfileStopReason::ConfidenceReached;
    mixed.avg_ms_per_iteration = 0.002;
    mixed.coefficient_of_variation = 0.8;
    mixed.tail_ratio = 2.0;
    mixed.regional_cost_ratio = 1.5;

    const auto trusted = policy.evaluate(mixed, 100'000);
    assert(!trusted.mean_capped);
    assert(trusted.mean_weight > guarded.mean_weight);
    assert(trusted.per_iteration_ms > 0.001);
    assert(trusted.per_iteration_ms < 0.002);

    FunctionProfile stable = mixed;
    stable.avg_ms_per_iteration = 0.00102;
    stable.coefficient_of_variation = 0.02;
    stable.tail_ratio = 1.01;
    stable.regional_cost_ratio = 1.0;
    const auto unchanged = policy.evaluate(stable, 10'000);
    assert(std::abs(unchanged.per_iteration_ms - 0.001) < 0.00001);

    return 0;
}
