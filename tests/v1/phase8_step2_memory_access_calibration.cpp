#include <cassert>
#include <cmath>
#include <vector>

#include <smart/decision/memory_access_calibration.hpp>
#include <smart/workload/workload_builder.hpp>
#include <smart/workload/workload_analyzer.hpp>

int main()
{
    using namespace smart;

    // Production-path regression: indexed container capability must not be
    // interpreted as a semantic random-memory-access observation.
    std::vector<int> values(64'000);
    const Workload vector_workload = WorkloadBuilder::container(values);
    assert(vector_workload.dimensions.size() == 1);
    assert(vector_workload.dimensions.front().contiguous_known);
    assert(vector_workload.dimensions.front().contiguous);
    assert(!vector_workload.dimensions.front().random_access_known);
    assert(!vector_workload.dimensions.front().random_access);

    const WorkloadAnalysis vector_analysis =
        WorkloadAnalyzer().analyze(vector_workload);
    assert(vector_analysis.structural.dimensions.size() == 1);
    assert(!vector_analysis.structural.dimensions.front().random_access_known);

    const Workload range_workload = WorkloadBuilder::index_range(64'000);
    assert(range_workload.dimensions.size() == 1);
    assert(!range_workload.dimensions.front().random_access_known);

    PerformanceModel model;
    model.hardware.logical_threads = 16;
    model.hardware.physical_cores = 8;
    model.l3_pressure = 8.0;

    WorkloadFamilyClassification streaming;
    streaming.family = WorkloadFamily::StreamingMemory;
    streaming.confidence = 0.90;

    WorkloadAnalysis bandwidth;
    bandwidth.structural.logical_iterations = 500'000;
    bandwidth.structural.represented_input_bytes = 32 * 1024 * 1024;
    bandwidth.structural.cache_ratios_available = true;
    bandwidth.structural.l3_residency_ratio = 4.0;
    DimensionAnalysis contiguous;
    contiguous.storage_kind = StorageKind::Contiguous;
    contiguous.contiguous_known = true;
    contiguous.contiguous = true;
    contiguous.random_access_known = false;
    contiguous.random_access = false;
    bandwidth.structural.dimensions.push_back(contiguous);

    ExecutionPlan eight;
    eight.parallel = true;
    eight.job_count = 8;
    eight.engine = ExecutionEngineType::OneTbb;
    eight.strategy = ExecutionStrategy::DynamicChunks;

    ExecutionPlan sixteen = eight;
    sixteen.job_count = 16;

    MemoryAccessCalibrationPolicy policy;
    const auto bw8 = policy.evaluate(eight, streaming, bandwidth, model);
    const auto bw16 = policy.evaluate(sixteen, streaming, bandwidth, model);
    assert(bw8.regime == MemoryAccessRegime::BandwidthBound);
    assert(bw16.regime == MemoryAccessRegime::BandwidthBound);
    assert(!bw8.applied);
    assert(!bw16.applied);
    assert(std::abs(bw8.factor - 1.0) < 1.0e-12);
    assert(std::abs(bw16.factor - 1.0) < 1.0e-12);

    WorkloadFamilyClassification irregular = streaming;
    irregular.family = WorkloadFamily::IrregularMemory;
    WorkloadAnalysis latency = bandwidth;
    latency.structural.dimensions.front().contiguous = false;
    latency.structural.dimensions.front().random_access_known = true;
    latency.structural.dimensions.front().random_access = true;
    const auto random = policy.evaluate(sixteen, irregular, latency, model);
    assert(random.regime == MemoryAccessRegime::LatencyBound);
    assert(random.factor > 1.0);
    assert(random.factor <= 1.05);

    // An irregular family label without semantic access evidence must not
    // manufacture a latency-bound correction.
    WorkloadAnalysis unknown_randomness = bandwidth;
    const auto unknown = policy.evaluate(
        sixteen, irregular, unknown_randomness, model);
    assert(unknown.regime == MemoryAccessRegime::BandwidthBound);
    assert(!unknown.applied);

    WorkloadAnalysis cache = bandwidth;
    cache.structural.logical_iterations = 64'000;
    cache.structural.represented_input_bytes = 256 * 1024;
    cache.structural.l3_residency_ratio = 0.10;
    const auto resident = policy.evaluate(eight, streaming, cache, model);
    assert(resident.regime == MemoryAccessRegime::CacheResident);
    assert(resident.factor < 1.0);
    assert(resident.factor >= 0.97);

    WorkloadAnalysis records = bandwidth;
    records.objects_are_large = true;
    records.structural.logical_iterations = 8'000;
    records.structural.represented_input_bytes = 8'000 * 256;
    records.structural.l3_residency_ratio = 2.0;
    const auto large = policy.evaluate(sixteen, streaming, records, model);
    assert(large.regime == MemoryAccessRegime::LargeRecord);
    assert(large.factor > 1.0);
    assert(large.factor <= 1.05);

    PlanCostEstimate estimate;
    estimate.plan = sixteen;
    estimate.useful_work_ms = 100.0;
    estimate.predicted_execution_ms = 20.0;
    estimate.memory_penalty_ms = 5.0;
    estimate.imbalance_penalty_ms = 1.0;
    estimate.scheduling_overhead_ms = 1.0;
    estimate.framework_overhead_ms = 1.0;
    estimate.predicted_total_ms = 23.0;
    estimate.confidence = 0.8;
    const double original_confidence = estimate.confidence;
    const double original_memory_penalty = estimate.memory_penalty_ms;
    policy.apply(estimate, irregular, latency, model);
    assert(estimate.memory_access_calibration_applied);
    assert(estimate.predicted_execution_ms > 20.0);
    assert(estimate.memory_access_calibration_confidence > 0.0);
    assert(std::abs(estimate.confidence - original_confidence) < 1.0e-12);
    assert(std::abs(estimate.memory_penalty_ms - original_memory_penalty) < 1.0e-12);
    return 0;
}
