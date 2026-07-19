#pragma once

#include <cstddef>
#include <smart/experience/experience_entry.hpp>
#include <smart/experience/experience_plan_key.hpp>
#include <smart/workload/fingerprint.hpp>
#include <unordered_map>

namespace smart
{
struct ExperienceRecord
{
    WorkloadFingerprint fingerprint;

    std::unordered_map<ExperiencePlanKey, ExperienceEntry, ExperiencePlanKeyHash> plans;

    bool valid = false;
};
} // namespace smart
