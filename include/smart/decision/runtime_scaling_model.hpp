#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <smart/profiling/function_profiler.hpp>
#include <smart/workload/workload_family.hpp>

namespace smart
{
    struct RuntimeScalingResult
    {
        bool applied = false;
        double base_total_work_ms = 0.0;
        double scaled_total_work_ms = 0.0;
        double scaling_exponent = 1.0;
        double correction_factor = 1.0;
        double confidence = 0.0;
        double extrapolation_ratio = 1.0;
    };

    // Phase 10: bounded learned scaling model.
    //
    // The profiler measures a sparse subset of a workload. Linear
    // extrapolation remains the neutral baseline, while profile shape and
    // workload-family evidence estimate a small growth exponent correction.
    // The correction is intentionally bounded because a single profile must
    // never dominate established analytical and historical safeguards.
    class RuntimeScalingPolicy
    {
    public:
        RuntimeScalingResult evaluate(
            double linear_total_work_ms,
            std::size_t iterations,
            const FunctionProfile& profile,
            const WorkloadFamilyClassification& family) const
        {
            RuntimeScalingResult result;
            result.base_total_work_ms = linear_total_work_ms;
            result.scaled_total_work_ms = linear_total_work_ms;

            if (!std::isfinite(linear_total_work_ms) || linear_total_work_ms <= 0.0 ||
                iterations == 0 || profile.callback_invocations == 0)
            {
                return result;
            }

            const double sampled = static_cast<double>(
                std::min<std::size_t>(iterations, profile.callback_invocations));
            const double full = static_cast<double>(iterations);
            const double ratio = std::max(1.0, full / std::max(1.0, sampled));
            result.extrapolation_ratio = ratio;

            if (ratio <= 1.25)
                return result;

            double prior_exponent = 1.0;
            switch (family.family)
            {
            case WorkloadFamily::ComputeHeavy:
                prior_exponent = 1.00;
                break;
            case WorkloadFamily::StreamingMemory:
                prior_exponent = 0.94;
                break;
            case WorkloadFamily::IrregularMemory:
                prior_exponent = 1.04;
                break;
            case WorkloadFamily::BranchHeavy:
                prior_exponent = 1.01;
                break;
            case WorkloadFamily::Mixed:
                prior_exponent = 0.99;
                break;
            case WorkloadFamily::Unknown:
                prior_exponent = 1.00;
                break;
            }

            double observed_exponent = 1.0;
            double shape_confidence = 0.0;
            if (profile.spatial_observations_available &&
                profile.local_median_ms_per_iteration > 0.0 &&
                profile.distributed_median_ms_per_iteration > 0.0)
            {
                const double spatial_ratio = std::clamp(
                    profile.distributed_median_ms_per_iteration /
                        profile.local_median_ms_per_iteration,
                    0.50,
                    2.00);
                // Translate measured spatial drift into a conservative growth
                // exponent adjustment. log2 keeps reciprocal changes symmetric.
                observed_exponent = 1.0 + 0.06 * std::log2(spatial_ratio);
                shape_confidence = 0.55;
            }

            const double reliability = profile.measurement_reliable ? 1.0 : 0.45;
            const double sample_evidence = std::clamp(
                static_cast<double>(profile.measured_batches) / 12.0,
                0.0,
                1.0);
            const double family_confidence = std::clamp(family.confidence, 0.0, 1.0);
            result.confidence = std::clamp(
                reliability * sample_evidence *
                    (0.35 + 0.40 * family_confidence + 0.25 * shape_confidence),
                0.0,
                1.0);

            const double learned_exponent =
                prior_exponent * (1.0 - shape_confidence) +
                observed_exponent * shape_confidence;
            result.scaling_exponent = std::clamp(
                1.0 + (learned_exponent - 1.0) * result.confidence,
                0.90,
                1.08);

            const double raw_factor = std::pow(
                ratio,
                result.scaling_exponent - 1.0);
            result.correction_factor = std::clamp(raw_factor, 0.72, 1.35);
            result.scaled_total_work_ms =
                linear_total_work_ms * result.correction_factor;
            result.applied = std::abs(result.correction_factor - 1.0) > 1.0e-6;
            return result;
        }
    };
}
