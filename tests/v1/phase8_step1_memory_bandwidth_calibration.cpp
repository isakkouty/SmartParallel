#include <cassert>
#include <cmath>

#include <smart/decision/memory_bandwidth_calibration.hpp>

int main()
{
    using namespace smart;

    PerformanceModel model;
    model.hardware.logical_threads = 16;
    model.hardware.physical_cores = 8;
    model.hardware.l3_cache_size = 16 * 1024 * 1024;
    model.l3_pressure = 16.0;

    WorkloadAnalysis analysis;
    analysis.objects_are_large = true;
    analysis.structural.logical_iterations = 1'000'000;
    analysis.structural.represented_input_bytes = 256 * 1024 * 1024;
    analysis.structural.cache_ratios_available = true;
    analysis.structural.l3_residency_ratio = 16.0;

    WorkloadFamilyClassification streaming;
    streaming.family = WorkloadFamily::StreamingMemory;
    streaming.confidence = 0.90;
    streaming.ambiguous = false;

    ExecutionPlan four_workers;
    four_workers.parallel = true;
    four_workers.job_count = 4;
    four_workers.engine = ExecutionEngineType::OneTbb;
    four_workers.strategy = ExecutionStrategy::DynamicChunks;

    ExecutionPlan sixteen_workers = four_workers;
    sixteen_workers.job_count = 16;

    ExecutionPlan static_sixteen = sixteen_workers;
    static_sixteen.engine = ExecutionEngineType::StaticThread;
    static_sixteen.strategy = ExecutionStrategy::StaticChunks;

    MemoryBandwidthCalibrationPolicy policy;
    const auto low = policy.evaluate(
        four_workers, streaming, analysis, model);
    const auto high = policy.evaluate(
        sixteen_workers, streaming, analysis, model);
    const auto statik = policy.evaluate(
        static_sixteen, streaming, analysis, model);

    assert(low.applied);
    assert(high.applied);
    assert(high.saturation_strength > 0.50);
    assert(high.estimated_saturation_workers < 8);
    assert(high.worker_excess > low.worker_excess);
    assert(high.factor > low.factor);
    assert(statik.factor > high.factor);
    assert(high.bytes_per_iteration > 128.0);

    WorkloadFamilyClassification irregular = streaming;
    irregular.family = WorkloadFamily::IrregularMemory;
    const auto ignored = policy.evaluate(
        sixteen_workers, irregular, analysis, model);
    assert(!ignored.applied);
    assert(std::abs(ignored.factor - 1.0) < 1.0e-12);

    PlanCostEstimate estimate;
    estimate.plan = sixteen_workers;
    estimate.useful_work_ms = 100.0;
    estimate.predicted_execution_ms = 20.0;
    estimate.memory_penalty_ms = 4.0;
    estimate.imbalance_penalty_ms = 1.0;
    estimate.scheduling_overhead_ms = 2.0;
    estimate.framework_overhead_ms = 1.0;
    estimate.predicted_total_ms = 24.0;
    policy.apply(estimate, streaming, analysis, model);
    assert(estimate.memory_bandwidth_calibration_applied);
    assert(estimate.predicted_execution_ms > 20.0);
    assert(estimate.predicted_total_ms > 24.0);
    return 0;
}
