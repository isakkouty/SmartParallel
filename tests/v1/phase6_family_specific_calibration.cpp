#include <smart/decision/family_calibration.hpp>

#include <cassert>
#include <cmath>

namespace
{
    smart::PlanCostEstimate candidate(
        smart::ExecutionEngineType engine,
        smart::ExecutionStrategy strategy,
        std::size_t workers)
    {
        smart::PlanCostEstimate estimate;
        estimate.available = true;
        estimate.plan.engine = engine;
        estimate.plan.strategy = strategy;
        estimate.plan.parallel = strategy != smart::ExecutionStrategy::Sequential;
        estimate.plan.job_count = workers;
        estimate.useful_work_ms = 100.0;
        estimate.predicted_execution_ms = 20.0;
        estimate.imbalance_penalty_ms = 2.0;
        estimate.scheduling_overhead_ms = 1.0;
        estimate.framework_overhead_ms = 0.5;
        estimate.predicted_total_ms = 23.5;
        estimate.predicted_speedup = 5.0;
        return estimate;
    }

    smart::WorkloadAnalysis memory_analysis()
    {
        smart::WorkloadAnalysis analysis;
        analysis.structural.logical_iterations = 500000;
        analysis.structural.cache_ratios_available = true;
        analysis.structural.l3_residency_ratio = 16.0;
        return analysis;
    }
}

int main()
{
    smart::FamilyCalibrationPolicy policy;
    const smart::WorkloadAnalysis analysis = memory_analysis();
    smart::PerformanceModel model;
    model.l3_pressure = 16.0;
    model.working_set_exceeds_l3 = true;

    smart::WorkloadFamilyClassification streaming;
    streaming.family = smart::WorkloadFamily::StreamingMemory;
    streaming.confidence = 1.0;

    const smart::FamilyCalibrationResult low_worker = policy.evaluate(
        candidate(
            smart::ExecutionEngineType::OneTbb,
            smart::ExecutionStrategy::DynamicChunks,
            4).plan,
        streaming,
        analysis,
        model);

    const smart::FamilyCalibrationResult high_worker = policy.evaluate(
        candidate(
            smart::ExecutionEngineType::OneTbb,
            smart::ExecutionStrategy::DynamicChunks,
            16).plan,
        streaming,
        analysis,
        model);

    assert(high_worker.factor > low_worker.factor);
    assert(high_worker.factor <= 1.18);

    smart::WorkloadFamilyClassification compute;
    compute.family = smart::WorkloadFamily::ComputeHeavy;
    compute.confidence = 1.0;

    const smart::FamilyCalibrationResult compute_high = policy.evaluate(
        candidate(
            smart::ExecutionEngineType::OneTbb,
            smart::ExecutionStrategy::DynamicChunks,
            16).plan,
        compute,
        analysis,
        model);
    assert(compute_high.factor < 1.0);

    smart::WorkloadFamilyClassification irregular;
    irregular.family = smart::WorkloadFamily::IrregularMemory;
    irregular.confidence = 1.0;

    const smart::FamilyCalibrationResult irregular_static = policy.evaluate(
        candidate(
            smart::ExecutionEngineType::StaticThread,
            smart::ExecutionStrategy::StaticChunks,
            8).plan,
        irregular,
        analysis,
        model);
    const smart::FamilyCalibrationResult irregular_dynamic = policy.evaluate(
        candidate(
            smart::ExecutionEngineType::ThreadPool,
            smart::ExecutionStrategy::DynamicChunks,
            8).plan,
        irregular,
        analysis,
        model);
    assert(irregular_static.factor > irregular_dynamic.factor);

    smart::PlanCostEstimate applied = candidate(
        smart::ExecutionEngineType::OneTbb,
        smart::ExecutionStrategy::DynamicChunks,
        16);
    const double original = applied.predicted_total_ms;
    policy.apply(applied, streaming, analysis, model);

    assert(applied.family_calibration_applied);
    assert(applied.workload_family == smart::WorkloadFamily::StreamingMemory);
    assert(applied.predicted_total_ms > original);
    assert(std::abs(
        applied.family_calibration_factor - high_worker.factor) < 1e-12);

    smart::WorkloadFamilyClassification uncertain = streaming;
    uncertain.confidence = 0.0;
    const smart::FamilyCalibrationResult ignored = policy.evaluate(
        applied.plan,
        uncertain,
        analysis,
        model);
    assert(!ignored.applied);
    assert(ignored.factor == 1.0);

    return 0;
}
