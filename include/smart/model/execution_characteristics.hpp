#pragma once

namespace smart
{
    enum class SchedulingPreference
    {
        Unknown,
        Sequential,
        Static,
        Dynamic
    };

    struct ExecutionCharacteristics
    {
        SchedulingPreference scheduling =
            SchedulingPreference::Unknown;

        bool memory_locality_critical = false;
        bool scheduler_overhead_sensitive = false;
        bool load_balancing_important = false;
    };
}
