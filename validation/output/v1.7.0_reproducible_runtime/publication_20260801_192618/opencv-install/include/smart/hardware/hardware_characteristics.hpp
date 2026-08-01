#pragma once

#include <cstddef>
#include <smart/hardware/hardware.hpp>

namespace smart
{
struct HardwareCharacteristics
{
    std::size_t logical_threads = 1;
    std::size_t physical_cores = 1;

    std::size_t numa_nodes = 1;
    std::size_t page_size = 4096;

    std::size_t cache_line_size = 64;

    std::size_t l1_cache_size = 0;
    std::size_t l2_cache_size = 0;
    std::size_t l3_cache_size = 0;

    bool cache_info_available = false;
    bool numa_info_available = false;
    bool page_size_available = false;
};

// Returns the best native topology information available on the host.
// Every platform path is conservative: unavailable values retain the defaults
// above and their corresponding availability flag remains false.
HardwareCharacteristics hardware_characteristics();
} // namespace smart
