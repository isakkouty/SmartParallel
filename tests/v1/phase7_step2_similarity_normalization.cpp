#include <smart/workload/fingerprint.hpp>
#include <smart/workload/fingerprint_similarity.hpp>

#include <cassert>
#include <cmath>
#include <cstddef>

namespace
{
    smart::WorkloadFingerprint make_fingerprint(
        std::size_t value,
        std::size_t iterations,
        std::size_t bytes,
        std::size_t object_size,
        double function_cost,
        double variation)
    {
        smart::WorkloadFingerprint fingerprint;
        fingerprint.value = value;
        fingerprint.kind_bucket = 1;
        fingerprint.iteration_bucket = smart::fingerprint_bucket(iterations);
        fingerprint.working_set_bucket = smart::fingerprint_bucket(bytes);
        fingerprint.object_size_bucket = smart::fingerprint_bucket(object_size);
        fingerprint.function_cost_bucket =
            smart::fingerprint_real_bucket(function_cost);
        fingerprint.variation_bucket =
            smart::fingerprint_real_bucket(variation);
        return fingerprint;
    }
}

int main()
{
    const smart::WorkloadFingerprint baseline = make_fingerprint(
        1,
        100000,
        8 * 1024 * 1024,
        32,
        0.001,
        1.20);

    const smart::WorkloadFingerprint close = make_fingerprint(
        2,
        140000,
        12 * 1024 * 1024,
        32,
        0.0013,
        1.30);

    const smart::WorkloadFingerprint farther = make_fingerprint(
        3,
        1600000,
        128 * 1024 * 1024,
        256,
        0.020,
        4.00);

    const smart::FingerprintSimilarity same =
        smart::compare_fingerprints(baseline, baseline);
    const smart::FingerprintSimilarity nearby =
        smart::compare_fingerprints(baseline, close);
    const smart::FingerprintSimilarity distant =
        smart::compare_fingerprints(baseline, farther);

    assert(same.compatible_kind);
    assert(same.normalized);
    assert(std::abs(same.total - 1.0) < 1e-12);
    assert(same.evidence_coverage > 0.99);
    assert(same.normalized_distance < 1e-12);

    assert(nearby.total < same.total);
    assert(nearby.total > distant.total);
    assert(nearby.normalized_distance < distant.normalized_distance);
    assert(nearby.function_cost > distant.function_cost);
    assert(nearby.working_set > distant.working_set);

    // Real-valued buckets encode exponent and mantissa. Their raw integer
    // ratio is meaningless; normalized decoding must preserve scale ordering.
    const std::size_t cost_one = smart::fingerprint_real_bucket(0.001);
    const std::size_t cost_two = smart::fingerprint_real_bucket(0.002);
    const std::size_t cost_large = smart::fingerprint_real_bucket(0.100);
    assert(smart::real_bucket_similarity(cost_one, cost_two).similarity >
           smart::real_bucket_similarity(cost_one, cost_large).similarity);

    smart::WorkloadFingerprint unknown_left = baseline;
    smart::WorkloadFingerprint unknown_right = baseline;
    unknown_left.function_cost_bucket = 0;
    unknown_right.function_cost_bucket = 0;
    const smart::FingerprintSimilarity unknown =
        smart::compare_fingerprints(unknown_left, unknown_right);
    assert(unknown.total < 1.0);
    assert(unknown.evidence_coverage < 1.0);

    smart::WorkloadFingerprint incompatible = close;
    incompatible.kind_bucket = 99;
    const smart::FingerprintSimilarity rejected =
        smart::compare_fingerprints(baseline, incompatible);
    assert(!rejected.compatible_kind);
    assert(rejected.total == 0.0);

    return 0;
}
