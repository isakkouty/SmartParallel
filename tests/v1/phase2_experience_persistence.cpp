#include <smart/experience/experience_database.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

int main()
{
    const std::string path = "smartparallel_phase2_experience_test.db";
    std::remove(path.c_str());

    smart::ExperienceDatabase database;
    smart::WorkloadFingerprint fingerprint{123456u};

    smart::ExecutionPlan plan;
    plan.engine = smart::ExecutionEngineType::OneTbb;
    plan.strategy = smart::ExecutionStrategy::DynamicChunks;
    plan.parallel = true;
    plan.job_count = 8;

    database.record(fingerprint, plan, 12.0, 10.0);
    database.record(fingerprint, plan, 11.0, 10.0);
    database.record(fingerprint, plan, 13.0, 10.0);

    const smart::ExperienceEntry* entry =
        database.find_plan(fingerprint, plan);

    assert(entry != nullptr);
    assert(entry->sample_count == 3);
    assert(entry->prediction_sample_count == 3);
    assert(entry->average_runtime_correction > 1.0);
    assert(entry->average_absolute_prediction_error_percent > 0.0);
    assert(database.save_to_file(path));

    smart::ExperienceDatabase loaded;
    assert(loaded.load_from_file(path));

    const smart::ExperienceEntry* loaded_entry =
        loaded.find_plan(fingerprint, plan);

    assert(loaded_entry != nullptr);
    assert(loaded_entry->sample_count == 3);
    assert(loaded_entry->prediction_sample_count == 3);
    assert(std::abs(
        loaded_entry->average_runtime_correction -
        entry->average_runtime_correction) < 1e-9);

    std::remove(path.c_str());
    return 0;
}
