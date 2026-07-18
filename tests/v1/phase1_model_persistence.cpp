#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <smart/ranking/utility_model_artifact.hpp>

int main()
{
    smart::ranking::UtilityModelArtifact original;
    original.feature_schema = "phase1_utility_v1";
    original.promotion_status = "PROMOTED";
    original.hardware_fingerprint = "test-machine";
    original.scaler_means = {10.0, 20.0};
    original.scaler_scales = {2.0, 5.0};
    original.model = smart::ranking::LinearUtilityModel(3);
    original.model.weights() = {0.5, -1.25, 2.0};

    const std::string path = "smartparallel_model_roundtrip.spm";
    smart::ranking::save_utility_model_artifact(original, path);
    const auto loaded = smart::ranking::load_utility_model_artifact(path);

    assert(loaded.promoted());
    assert(loaded.feature_schema == original.feature_schema);
    assert(loaded.hardware_fingerprint == original.hardware_fingerprint);
    assert(loaded.model.weights() == original.model.weights());
    const std::vector<double> transformed = loaded.transform({12.0, 15.0});
    assert(transformed.size() == 3);
    assert(std::abs(transformed[0] - 1.0) < 1.0e-12);
    assert(std::abs(transformed[1] - 1.0) < 1.0e-12);
    assert(std::abs(transformed[2] + 1.0) < 1.0e-12);
    assert(std::isfinite(loaded.model.score(transformed)));

    std::remove(path.c_str());
    return 0;
}
