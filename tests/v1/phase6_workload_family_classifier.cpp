#include <smart/decision/execution_hints.hpp>
#include <smart/model/performance_model.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_family.hpp>

#include <cassert>
#include <vector>

namespace
{
    smart::WorkloadAnalysis base_analysis()
    {
        smart::WorkloadAnalysis analysis;
        analysis.iterations = 100000;
        analysis.structural.logical_iterations = 100000;
        analysis.structural.represented_input_bytes = 4 * 1024 * 1024;
        analysis.structural.cache_ratios_available = true;
        analysis.structural.l3_residency_ratio = 0.25;
        return analysis;
    }

    smart::PerformanceModel base_model()
    {
        smart::PerformanceModel model;
        model.l3_pressure = 0.25;
        return model;
    }
}

int main()
{
    smart::WorkloadFamilyClassifier classifier;

    {
        smart::WorkloadAnalysis analysis = base_analysis();
        smart::PerformanceModel model = base_model();
        smart::ExecutionHints hints = smart::compute_heavy();

        const smart::WorkloadFamilyClassification result =
            classifier.classify(analysis, model, nullptr, &hints);

        assert(result.family == smart::WorkloadFamily::ComputeHeavy);
        assert(result.confidence > 0.35);
        assert(result.used_execution_hints);
    }

    {
        smart::WorkloadAnalysis analysis = base_analysis();
        smart::PerformanceModel model = base_model();
        model.l3_pressure = 8.0;
        model.working_set_exceeds_l3 = true;
        model.likely_memory_sensitive = true;
        analysis.structural.l3_residency_ratio = 8.0;

        smart::DimensionAnalysis dimension;
        dimension.storage_kind = smart::StorageKind::Contiguous;
        dimension.contiguous_known = true;
        dimension.contiguous = true;
        dimension.random_access_known = true;
        dimension.random_access = false;
        dimension.stride_known = true;
        dimension.stride_bytes = sizeof(int);
        analysis.structural.dimensions.push_back(dimension);

        const smart::WorkloadFamilyClassification result =
            classifier.classify(analysis, model);

        assert(result.family == smart::WorkloadFamily::StreamingMemory);
        assert(result.used_structural_observations);
    }

    {
        smart::WorkloadAnalysis analysis = base_analysis();
        smart::PerformanceModel model = base_model();
        smart::ExecutionHints hints = smart::memory_random();

        smart::DimensionAnalysis dimension;
        dimension.storage_kind = smart::StorageKind::NodeBased;
        dimension.random_access_known = true;
        dimension.random_access = true;
        analysis.structural.dimensions.push_back(dimension);

        smart::FunctionProfile profile;
        profile.available = true;
        profile.coefficient_of_variation = 0.8;
        profile.tail_ratio = 2.5;
        profile.regional_cost_ratio = 2.0;

        const smart::WorkloadFamilyClassification result =
            classifier.classify(analysis, model, &profile, &hints);

        assert(result.family == smart::WorkloadFamily::IrregularMemory);
        assert(result.used_profile_observations);
    }

    {
        smart::WorkloadAnalysis analysis = base_analysis();
        smart::PerformanceModel model = base_model();
        smart::ExecutionHints hints;
        hints.available = true;
        hints.branchiness = 1.0;

        const smart::WorkloadFamilyClassification result =
            classifier.classify(analysis, model, nullptr, &hints);

        assert(result.family == smart::WorkloadFamily::BranchHeavy);
    }

    {
        smart::WorkloadAnalysis analysis = base_analysis();
        smart::PerformanceModel model = base_model();
        smart::ExecutionHints hints;
        hints.available = true;
        hints.arithmetic_intensity = 0.7;
        hints.memory_randomness = 0.7;

        const smart::WorkloadFamilyClassification result =
            classifier.classify(analysis, model, nullptr, &hints);

        assert(result.family == smart::WorkloadFamily::Mixed);
        assert(result.ambiguous || result.evidence.mixed > 0.0);
    }

    return 0;
}
