#pragma once

#include <cstddef>
#include <cstdint>
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

    // In-memory bounded-cache metadata. It is intentionally not serialized.
    std::uint64_t last_access_epoch = 0;
    bool valid = false;
};
} // namespace smart
