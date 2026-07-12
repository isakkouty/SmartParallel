#pragma once

#include <functional>

#include <smart/workload/workload.hpp>
#include <smart/hardware/hardware_characteristics.hpp>

namespace smart
{
    struct WorkloadFingerprint
    {
        std::size_t value = 0;
    };

    inline std::size_t fingerprint_bucket(std::size_t value)
    {
        if (value == 0)
            return 0;

        std::size_t bucket = 1;

        while (bucket < value)
        {
            bucket <<= 1;
        }

        return bucket;
    }

    inline WorkloadFingerprint fingerprint(const Workload& workload)
    {
        std::size_t seed = 0;

        auto combine = [&](std::size_t value)
        {
            seed ^= std::hash<std::size_t>{}(value)
                + 0x9e3779b9
                + (seed << 6)
                + (seed >> 2);
        };

        combine(static_cast<std::size_t>(workload.kind));
        combine(fingerprint_bucket(workload.iterations));

        for (const Dimension& dimension : workload.dimensions)
        {
            combine(fingerprint_bucket(dimension.size));
            combine(fingerprint_bucket(dimension.object_size));
        }

        HardwareCharacteristics hw = hardware_characteristics();

        combine(fingerprint_bucket(hw.logical_threads));
        combine(fingerprint_bucket(hw.physical_cores));
        combine(fingerprint_bucket(hw.l1_cache_size));
        combine(fingerprint_bucket(hw.l2_cache_size));
        combine(fingerprint_bucket(hw.l3_cache_size));
        combine(fingerprint_bucket(hw.cache_line_size));
        combine(fingerprint_bucket(hw.page_size));
        combine(fingerprint_bucket(hw.numa_nodes));

        return { seed };
    }
}
