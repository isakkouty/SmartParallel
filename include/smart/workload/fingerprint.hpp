#pragma once

#include <cmath>
#include <cstddef>
#include <functional>

#include <smart/workload/workload.hpp>
#include <smart/hardware/hardware_characteristics.hpp>
#include <smart/profiling/function_profiler.hpp>

namespace smart
{
    struct WorkloadFingerprint
    {
        std::size_t value = 0;

        // Coarse, non-identifying descriptors used only for bounded
        // similarity transfer. The hash remains the exact-match identity.
        std::size_t kind_bucket = 0;
        std::size_t iteration_bucket = 0;
        std::size_t working_set_bucket = 0;
        std::size_t object_size_bucket = 0;
        std::size_t function_cost_bucket = 0;
        std::size_t variation_bucket = 0;
        std::size_t access_pattern_bucket = 0;
        std::size_t stride_bucket = 0;
        std::size_t cache_pressure_bucket = 0;
        std::size_t topology_bucket = 0;
        std::size_t profile_shape_bucket = 0;
    };

    inline std::size_t fingerprint_bucket(std::size_t value)
    {
        if (value == 0)
            return 0;

        std::size_t bucket = 1;

        while (bucket < value &&
               bucket <= (static_cast<std::size_t>(-1) >> 1))
        {
            bucket <<= 1;
        }

        return bucket;
    }

    inline std::size_t fingerprint_real_bucket(double value)
    {
        if (!std::isfinite(value) || value <= 0.0)
            return 0;

        int exponent = 0;
        const double mantissa = std::frexp(value, &exponent);
        const std::size_t mantissa_bucket = static_cast<std::size_t>(
            std::max(0.0, std::min(15.0, std::floor(mantissa * 16.0))));
        const std::size_t exponent_bucket = static_cast<std::size_t>(
            exponent + 2048);

        return (exponent_bucket << 4) | mantissa_bucket;
    }

    inline void fingerprint_combine(std::size_t& seed, std::size_t value)
    {
        seed ^= std::hash<std::size_t>{}(value)
            + 0x9e3779b9
            + (seed << 6)
            + (seed >> 2);
    }

    inline WorkloadFingerprint fingerprint(
        const Workload& workload,
        const FunctionProfile* profile)
    {
        std::size_t seed = 0;

        fingerprint_combine(seed, static_cast<std::size_t>(workload.kind));
        fingerprint_combine(seed, fingerprint_bucket(workload.iterations));

        for (const Dimension& dimension : workload.dimensions)
        {
            fingerprint_combine(seed, fingerprint_bucket(dimension.size));
            fingerprint_combine(seed, fingerprint_bucket(dimension.object_size));
            fingerprint_combine(seed, static_cast<std::size_t>(dimension.storage_kind));
        }

        const HardwareCharacteristics hw = hardware_characteristics();

        fingerprint_combine(seed, fingerprint_bucket(hw.logical_threads));
        fingerprint_combine(seed, fingerprint_bucket(hw.physical_cores));
        fingerprint_combine(seed, fingerprint_bucket(hw.l1_cache_size));
        fingerprint_combine(seed, fingerprint_bucket(hw.l2_cache_size));
        fingerprint_combine(seed, fingerprint_bucket(hw.l3_cache_size));
        fingerprint_combine(seed, fingerprint_bucket(hw.cache_line_size));
        fingerprint_combine(seed, fingerprint_bucket(hw.page_size));
        fingerprint_combine(seed, fingerprint_bucket(hw.numa_nodes));

        if (profile != nullptr && profile->available)
        {
            double cost = profile->steady_state_ms_per_iteration;
            if (cost <= 0.0)
                cost = profile->trimmed_mean_ms_per_iteration;
            if (cost <= 0.0)
                cost = profile->median_ms_per_iteration;

            fingerprint_combine(seed, fingerprint_real_bucket(cost));
            fingerprint_combine(
                seed,
                fingerprint_real_bucket(profile->coefficient_of_variation + 1.0));
            fingerprint_combine(
                seed,
                fingerprint_real_bucket(profile->tail_ratio));
            fingerprint_combine(
                seed,
                fingerprint_real_bucket(profile->regional_cost_ratio));
        }
        else
        {
            fingerprint_combine(seed, 0);
        }

        WorkloadFingerprint result;
        result.value = seed;
        result.kind_bucket = static_cast<std::size_t>(workload.kind);
        result.iteration_bucket = fingerprint_bucket(workload.iterations);

        std::size_t represented_bytes = 0;
        std::size_t largest_object = 0;
        for (const Dimension& dimension : workload.dimensions)
        {
            const std::size_t dimension_bytes =
                dimension.size > 0 && dimension.object_size > 0 &&
                dimension.size <= static_cast<std::size_t>(-1) / dimension.object_size
                    ? dimension.size * dimension.object_size
                    : 0;
            represented_bytes += dimension_bytes;
            largest_object = std::max(largest_object, dimension.object_size);
        }
        result.working_set_bucket = fingerprint_bucket(represented_bytes);
        result.object_size_bucket = fingerprint_bucket(largest_object);

        std::size_t access_seed = 0;
        std::size_t stride_seed = 0;
        for (const Dimension& dimension : workload.dimensions)
        {
            const std::size_t access_bits =
                (static_cast<std::size_t>(dimension.storage_kind) & 0x0f) |
                (dimension.contiguous_known ? 0x10 : 0) |
                (dimension.contiguous ? 0x20 : 0) |
                (dimension.random_access_known ? 0x40 : 0) |
                (dimension.random_access ? 0x80 : 0);
            fingerprint_combine(access_seed, access_bits);
            fingerprint_combine(
                stride_seed,
                dimension.stride_known
                    ? fingerprint_bucket(dimension.stride_bytes)
                    : 0);
        }
        result.access_pattern_bucket = access_seed;
        result.stride_bucket = stride_seed;

        const std::size_t effective_l3 = hw.l3_cache_size > 0
            ? hw.l3_cache_size
            : 32 * 1024 * 1024;
        result.cache_pressure_bucket = fingerprint_real_bucket(
            static_cast<double>(represented_bytes) /
            static_cast<double>(std::max<std::size_t>(1, effective_l3)) + 1.0);
        result.topology_bucket = 0;
        fingerprint_combine(result.topology_bucket, fingerprint_bucket(hw.physical_cores));
        fingerprint_combine(result.topology_bucket, fingerprint_bucket(hw.logical_threads));
        fingerprint_combine(result.topology_bucket, fingerprint_bucket(hw.numa_nodes));
        fingerprint_combine(result.topology_bucket, fingerprint_bucket(hw.cache_line_size));

        if (profile != nullptr && profile->available)
        {
            double cost = profile->steady_state_ms_per_iteration;
            if (cost <= 0.0)
                cost = profile->trimmed_mean_ms_per_iteration;
            if (cost <= 0.0)
                cost = profile->median_ms_per_iteration;
            result.function_cost_bucket = fingerprint_real_bucket(cost);
            result.variation_bucket = fingerprint_real_bucket(
                1.0 + profile->coefficient_of_variation +
                std::max(0.0, profile->tail_ratio - 1.0) +
                std::max(0.0, profile->regional_cost_ratio - 1.0));
            result.profile_shape_bucket = 0;
            fingerprint_combine(
                result.profile_shape_bucket,
                fingerprint_real_bucket(profile->coefficient_of_variation + 1.0));
            fingerprint_combine(
                result.profile_shape_bucket,
                fingerprint_real_bucket(profile->tail_ratio));
            fingerprint_combine(
                result.profile_shape_bucket,
                fingerprint_real_bucket(profile->regional_cost_ratio));
        }
        return result;
    }

    inline WorkloadFingerprint fingerprint(const Workload& workload)
    {
        return fingerprint(workload, nullptr);
    }
}
