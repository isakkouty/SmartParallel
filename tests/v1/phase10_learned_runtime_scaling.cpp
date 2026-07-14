#include <smart/decision/runtime_scaling_model.hpp>

#include <cassert>
#include <cmath>

int main()
{
    smart::FunctionProfile profile;
    profile.available = true;
    profile.measurement_reliable = true;
    profile.measured_batches = 16;
    profile.callback_invocations = 1024;
    profile.spatial_observations_available = true;
    profile.local_median_ms_per_iteration = 0.0010;
    profile.distributed_median_ms_per_iteration = 0.0012;

    smart::WorkloadFamilyClassification streaming;
    streaming.family = smart::WorkloadFamily::StreamingMemory;
    streaming.confidence = 0.9;

    const auto stream = smart::RuntimeScalingPolicy().evaluate(
        100.0, 1'000'000, profile, streaming);
    assert(stream.applied);
    assert(stream.scaling_exponent < 1.0);
    assert(stream.correction_factor >= 0.72);
    assert(stream.correction_factor <= 1.35);
    assert(stream.scaled_total_work_ms < stream.base_total_work_ms);

    smart::WorkloadFamilyClassification irregular;
    irregular.family = smart::WorkloadFamily::IrregularMemory;
    irregular.confidence = 0.9;
    const auto random = smart::RuntimeScalingPolicy().evaluate(
        100.0, 1'000'000, profile, irregular);
    assert(random.scaling_exponent >= stream.scaling_exponent);
    assert(random.correction_factor >= stream.correction_factor);

    profile.callback_invocations = 900'000;
    const auto near_full = smart::RuntimeScalingPolicy().evaluate(
        100.0, 1'000'000, profile, streaming);
    assert(!near_full.applied);
    assert(std::abs(near_full.correction_factor - 1.0) < 1.0e-12);

    return 0;
}
