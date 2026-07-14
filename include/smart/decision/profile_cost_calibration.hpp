#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <smart/profiling/function_profiler.hpp>

namespace smart
{
    struct ProfileCostCalibrationResult
    {
        double per_iteration_ms = 0.0;
        double total_work_ms = 0.0;
        double baseline_ms = 0.0;
        double mean_ms = 0.0;
        double mean_weight = 0.0;
        double mean_ratio = 1.0;
        bool mean_blended = false;
        bool mean_capped = false;
    };

    // Phase 9 step 1: robust profile extrapolation.
    //
    // Profiling occasionally observes a small number of slow samples. The
    // arithmetic mean is useful evidence for genuinely mixed work, but it
    // must not be allowed to multiply the full-workload estimate when the
    // sample set is sparse or unreliable. This policy keeps the robust
    // steady/trimmed/median estimator authoritative and admits only bounded,
    // evidence-weighted influence from the mean.
    class ProfileCostCalibrationPolicy
    {
    public:
        ProfileCostCalibrationResult evaluate(
            const FunctionProfile& profile,
            std::size_t iterations) const
        {
            ProfileCostCalibrationResult result;

            double baseline = profile.steady_state_ms_per_iteration;
            if (!finite_positive(baseline))
                baseline = profile.trimmed_mean_ms_per_iteration;
            if (!finite_positive(baseline))
                baseline = profile.median_ms_per_iteration;

            result.baseline_ms = finite_positive(baseline) ? baseline : 0.0;
            result.mean_ms = finite_positive(profile.avg_ms_per_iteration)
                ? profile.avg_ms_per_iteration
                : 0.0;

            double per_iteration = result.baseline_ms;
            if (finite_positive(per_iteration) &&
                finite_positive(result.mean_ms))
            {
                const double raw_ratio = result.mean_ms / per_iteration;
                const double capped_ratio = std::clamp(raw_ratio, 0.50, 3.00);
                result.mean_ratio = capped_ratio;
                result.mean_capped = std::abs(capped_ratio - raw_ratio) > 1.0e-12;

                double evidence = 1.0;
                if (!profile.measurement_reliable)
                    evidence *= 0.35;
                if (profile.measured_batches < 4)
                    evidence *= 0.35;
                else if (profile.measured_batches < 8)
                    evidence *= 0.70;

                if (profile.stop_reason == ProfileStopReason::TimeBudgetReached ||
                    profile.stop_reason == ProfileStopReason::InvocationBudgetReached ||
                    profile.stop_reason == ProfileStopReason::MeasurementUnreliable)
                {
                    evidence *= 0.65;
                }

                const double variation = std::clamp(
                    profile.coefficient_of_variation * 0.30 +
                    std::max(0.0, profile.tail_ratio - 1.0) * 0.12 +
                    std::max(0.0, profile.regional_cost_ratio - 1.0) * 0.08,
                    0.0,
                    0.45);

                result.mean_weight = std::clamp(variation * evidence, 0.0, 0.45);
                const double bounded_mean = per_iteration * capped_ratio;
                per_iteration =
                    per_iteration * (1.0 - result.mean_weight) +
                    bounded_mean * result.mean_weight;
                result.mean_blended = result.mean_weight > 1.0e-12;
            }

            if (!finite_positive(per_iteration) &&
                finite_positive(profile.estimated_total_work_ms) &&
                iterations > 0)
            {
                per_iteration = profile.estimated_total_work_ms /
                    static_cast<double>(iterations);
            }

            if (!finite_positive(per_iteration))
                per_iteration = result.mean_ms;

            if (!finite_positive(per_iteration) || iterations == 0)
                return result;

            result.per_iteration_ms = per_iteration;
            result.total_work_ms =
                std::max(0.0, profile.estimated_setup_cost_ms) +
                per_iteration * static_cast<double>(iterations);
            return result;
        }

    private:
        static bool finite_positive(double value)
        {
            return std::isfinite(value) && value > 0.0;
        }
    };
}
