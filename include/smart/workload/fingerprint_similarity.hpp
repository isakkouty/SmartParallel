#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <smart/workload/fingerprint.hpp>

namespace smart
{
struct FingerprintSimilarity
{
    double total = 0.0;
    double iteration = 0.0;

    double working_set = 0.0;
    double object_size = 0.0;
    double function_cost = 0.0;
    double variation = 0.0;
    double access_pattern = 0.0;
    double stride = 0.0;

    double cache_pressure = 0.0;
    double topology = 0.0;
    double profile_shape = 0.0;
    double normalized_distance = 1.0;
    double evidence_coverage = 0.0;
    bool compatible_kind = false;

    bool normalized = true;
};

struct SimilarityComponent
{
    double similarity = 0.0;
    double distance = 1.0;
    double coverage = 0.0;
};

inline SimilarityComponent logarithmic_similarity(double left_log2,
                                                  bool left_available,
                                                  double right_log2,
                                                  bool right_available,
                                                  double distance_scale)
{
    SimilarityComponent result;

    if (!left_available && !right_available)
    {
        // No evidence must not be mistaken for a perfect match. The
        // component is omitted from the weighted total instead.
        result.similarity = 0.0;
        result.distance = 0.0;
        result.coverage = 0.0;
        return result;
    }

    if (!left_available || !right_available)
    {
        result.similarity = 0.20;
        result.distance = 1.0;
        result.coverage = 0.50;
        return result;
    }

    const double safe_scale = std::max(0.25, distance_scale);
    const double log_distance = std::abs(left_log2 - right_log2);
    result.similarity = std::clamp(std::exp(-log_distance / safe_scale), 0.0, 1.0);
    result.distance = std::clamp(log_distance / safe_scale, 0.0, 1.0);
    result.coverage = 1.0;
    return result;
}

inline SimilarityComponent
size_bucket_similarity(std::size_t left, std::size_t right, double distance_scale = 2.0)
{
    return logarithmic_similarity(left > 0 ? std::log2(static_cast<double>(left)) : 0.0,
                                  left > 0,
                                  right > 0 ? std::log2(static_cast<double>(right)) : 0.0,
                                  right > 0,
                                  distance_scale);
}

inline bool decode_real_bucket_log2(std::size_t bucket, double& decoded_log2)
{
    if (bucket == 0)
        return false;

    const std::size_t mantissa_bucket = bucket & 0x0fu;
    const std::size_t exponent_bucket = bucket >> 4;
    if (exponent_bucket < 1024 || exponent_bucket > 3072)
        return false;

    const int exponent = static_cast<int>(exponent_bucket) - 2048;
    const double mantissa = (static_cast<double>(mantissa_bucket) + 0.5) / 16.0;
    if (!(mantissa > 0.0))
        return false;

    decoded_log2 = static_cast<double>(exponent) + std::log2(mantissa);
    return std::isfinite(decoded_log2);
}

inline SimilarityComponent
real_bucket_similarity(std::size_t left, std::size_t right, double distance_scale = 1.5)
{
    double left_log2 = 0.0;
    double right_log2 = 0.0;
    const bool left_available = decode_real_bucket_log2(left, left_log2);
    const bool right_available = decode_real_bucket_log2(right, right_log2);
    return logarithmic_similarity(
        left_log2, left_available, right_log2, right_available, distance_scale);
}

// Compatibility helper retained for callers that compare ordinary size
// buckets. Unlike the old raw ratio, this is scale-normalized in log space.
inline double fingerprint_bucket_similarity(std::size_t left, std::size_t right)
{
    return size_bucket_similarity(left, right).similarity;
}

inline FingerprintSimilarity compare_fingerprints(const WorkloadFingerprint& left,
                                                  const WorkloadFingerprint& right)
{
    FingerprintSimilarity result;
    result.compatible_kind = left.kind_bucket == right.kind_bucket;
    if (!result.compatible_kind)
        return result;

    const SimilarityComponent iteration =
        size_bucket_similarity(left.iteration_bucket, right.iteration_bucket, 2.0);

    const SimilarityComponent working_set =
        size_bucket_similarity(left.working_set_bucket, right.working_set_bucket, 2.0);
    const SimilarityComponent object_size =
        size_bucket_similarity(left.object_size_bucket, right.object_size_bucket, 1.5);
    const SimilarityComponent function_cost =
        real_bucket_similarity(left.function_cost_bucket, right.function_cost_bucket, 1.5);
    const SimilarityComponent variation =
        real_bucket_similarity(left.variation_bucket, right.variation_bucket, 2.0);
    const SimilarityComponent cache_pressure =
        real_bucket_similarity(left.cache_pressure_bucket, right.cache_pressure_bucket, 1.5);
    const SimilarityComponent stride =
        size_bucket_similarity(left.stride_bucket, right.stride_bucket, 1.5);

    const SimilarityComponent access_pattern{
        left.access_pattern_bucket == right.access_pattern_bucket ? 1.0 : 0.15,
        left.access_pattern_bucket == right.access_pattern_bucket ? 0.0 : 1.0,
        (left.access_pattern_bucket != 0 || right.access_pattern_bucket != 0) ? 1.0 : 0.0};
    const SimilarityComponent topology{
        left.topology_bucket == right.topology_bucket ? 1.0 : 0.35,
        left.topology_bucket == right.topology_bucket ? 0.0 : 0.75,
        (left.topology_bucket != 0 || right.topology_bucket != 0) ? 1.0 : 0.0};
    const SimilarityComponent profile_shape{
        left.profile_shape_bucket == right.profile_shape_bucket ? 1.0 : 0.25,
        left.profile_shape_bucket == right.profile_shape_bucket ? 0.0 : 0.75,
        (left.profile_shape_bucket != 0 || right.profile_shape_bucket != 0) ? 1.0 : 0.0};

    result.iteration = iteration.similarity;
    result.working_set = working_set.similarity;
    result.object_size = object_size.similarity;
    result.function_cost = function_cost.similarity;
    result.variation = variation.similarity;
    result.access_pattern = access_pattern.similarity;
    result.stride = stride.similarity;
    result.cache_pressure = cache_pressure.similarity;
    result.topology = topology.similarity;
    result.profile_shape = profile_shape.similarity;

    constexpr double iteration_weight = 0.16;

    constexpr double working_set_weight = 0.14;
    constexpr double object_size_weight = 0.07;
    constexpr double function_cost_weight = 0.20;
    constexpr double variation_weight = 0.09;
    constexpr double access_pattern_weight = 0.12;
    constexpr double stride_weight = 0.05;

    constexpr double cache_pressure_weight = 0.07;
    constexpr double topology_weight = 0.04;
    constexpr double profile_shape_weight = 0.06;

    const double weighted_coverage =
        iteration_weight * iteration.coverage + working_set_weight * working_set.coverage
        + object_size_weight * object_size.coverage + function_cost_weight * function_cost.coverage
        + variation_weight * variation.coverage + access_pattern_weight * access_pattern.coverage
        + stride_weight * stride.coverage + cache_pressure_weight * cache_pressure.coverage
        + topology_weight * topology.coverage + profile_shape_weight * profile_shape.coverage;

    const double weighted_similarity =
        iteration_weight * iteration.similarity * iteration.coverage
        + working_set_weight * working_set.similarity * working_set.coverage
        + object_size_weight * object_size.similarity * object_size.coverage
        + function_cost_weight * function_cost.similarity * function_cost.coverage
        + variation_weight * variation.similarity * variation.coverage
        + access_pattern_weight * access_pattern.similarity * access_pattern.coverage
        + stride_weight * stride.similarity * stride.coverage
        + cache_pressure_weight * cache_pressure.similarity * cache_pressure.coverage
        + topology_weight * topology.similarity * topology.coverage
        + profile_shape_weight * profile_shape.similarity * profile_shape.coverage;

    const double weighted_distance =
        iteration_weight * iteration.distance * iteration.coverage
        + working_set_weight * working_set.distance * working_set.coverage
        + object_size_weight * object_size.distance * object_size.coverage
        + function_cost_weight * function_cost.distance * function_cost.coverage
        + variation_weight * variation.distance * variation.coverage
        + access_pattern_weight * access_pattern.distance * access_pattern.coverage
        + stride_weight * stride.distance * stride.coverage
        + cache_pressure_weight * cache_pressure.distance * cache_pressure.coverage
        + topology_weight * topology.distance * topology.coverage
        + profile_shape_weight * profile_shape.distance * profile_shape.coverage;

    result.evidence_coverage = std::clamp(weighted_coverage, 0.0, 1.0);
    if (weighted_coverage <= 0.0)
        return result;

    const double normalized_similarity = weighted_similarity / weighted_coverage;
    const double coverage_penalty = 0.50 + 0.50 * result.evidence_coverage;
    result.total = std::clamp(normalized_similarity * coverage_penalty, 0.0, 1.0);
    result.normalized_distance = std::clamp(weighted_distance / weighted_coverage, 0.0, 1.0);
    return result;
}

inline double fingerprint_similarity(const WorkloadFingerprint& left,
                                     const WorkloadFingerprint& right)
{
    return compare_fingerprints(left, right).total;
}
} // namespace smart
