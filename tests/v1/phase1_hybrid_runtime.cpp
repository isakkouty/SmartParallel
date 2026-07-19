#include <cassert>
#include <cstdio>
#include <smart/core/config.hpp>
#include <smart/decision/decision_rules.hpp>
#include <smart/ranking/runtime_utility_policy.hpp>
#include <smart/ranking/utility_model_artifact.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_builder.hpp>
#include <vector>

int main()
{
    using namespace smart;
    using namespace smart::ranking;

    Workload workload = WorkloadBuilder::index_range(100000);
    WorkloadAnalyzer analyzer;
    WorkloadAnalysis analysis = analyzer.analyze(workload);
    DecisionContext context{workload, analysis, nullptr};

    ExecutionHints hints;

    DecisionReport analytical;
    analytical.plan.parallel = true;
    analytical.plan.engine = ExecutionEngineType::OneTbb;
    analytical.plan.strategy = ExecutionStrategy::DynamicChunks;
    analytical.plan.job_count = 8;
    analytical.plan.chunk_size = 128;

    const auto raw = make_runtime_raw_features(context, hints, analytical.plan);
    UtilityModelArtifact artifact;
    artifact.promotion_status = "PROMOTED";
    artifact.scaler_means.assign(raw.size(), 0.0);
    artifact.scaler_scales.assign(raw.size(), 1.0);
    artifact.model = LinearUtilityModel(raw.size() + 1);
    // Penalize oneTBB and reward Sequential strongly.
    artifact.model.weights()[23] = 4.0;  // oneTBB backend flag after intercept
    artifact.model.weights()[21] = -4.0; // Sequential backend flag after intercept

    const char* path = "smartparallel_hybrid_runtime_test.spm";
    save_utility_model_artifact(artifact, path);

    Config old = global_config();
    global_config().enable_utility_model_runtime = true;
    global_config().utility_model_file_path = path;
    global_config().minimum_utility_model_confidence = 0.50;

    RuntimeUtilityPolicy policy;
    const auto promoted = policy.choose(context, hints, analytical);
    assert(promoted.model_loaded);
    assert(promoted.model_promoted);
    assert(promoted.model_compatible);
    assert(promoted.applied);
    assert(!promoted.plan.parallel);
    assert(promoted.plan.strategy == ExecutionStrategy::Sequential);

    artifact.promotion_status = "SHADOW_ONLY";
    save_utility_model_artifact(artifact, path);
    const auto shadow = policy.choose(context, hints, analytical);
    assert(shadow.model_loaded);
    assert(!shadow.model_promoted);
    assert(!shadow.applied);
    assert(shadow.plan.engine == analytical.plan.engine);

    global_config() = old;
    std::remove(path);
    return 0;
}
