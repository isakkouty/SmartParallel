#pragma once

#include <cstddef>
#include <unordered_map>

#include <smart/experience/experience_entry.hpp>
#include <smart/experience/experience_plan_key.hpp>
#include <smart/workload/fingerprint.hpp>

namespace smart
{
    struct ExperienceRecord
    {
        WorkloadFingerprint fingerprint;

        std::unordered_map<
            ExperiencePlanKey,
            ExperienceEntry,
            ExperiencePlanKeyHash
        > plans;

        bool valid = false;
    };
}
