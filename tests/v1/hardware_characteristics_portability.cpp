#include <smart/hardware/hardware.hpp>
#include <smart/hardware/hardware_characteristics.hpp>

#include <cassert>
#include <cstddef>
#include <iostream>

namespace
{
bool plausible_power_of_two(std::size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}
}

int main()
{
    const smart::HardwareCharacteristics hardware = smart::hardware_characteristics();
    const std::size_t threads = smart::hardware_threads();

    assert(threads >= 1);
    assert(hardware.logical_threads >= 1);
    assert(hardware.physical_cores >= 1);
    assert(hardware.physical_cores <= hardware.logical_threads);
    assert(hardware.numa_nodes >= 1);
    assert(hardware.page_size >= 512);
    assert(hardware.cache_line_size >= 16);

    if (hardware.page_size_available)
        assert(plausible_power_of_two(hardware.page_size));

    if (hardware.cache_info_available)
    {
        assert(hardware.l1_cache_size > 0 || hardware.l2_cache_size > 0
               || hardware.l3_cache_size > 0);
        assert(plausible_power_of_two(hardware.cache_line_size));
    }

    std::cout << "logical_threads=" << hardware.logical_threads << '\n'
              << "physical_cores=" << hardware.physical_cores << '\n'
              << "numa_nodes=" << hardware.numa_nodes << '\n'
              << "page_size=" << hardware.page_size << '\n'
              << "cache_line_size=" << hardware.cache_line_size << '\n'
              << "l1_cache_size=" << hardware.l1_cache_size << '\n'
              << "l2_cache_size=" << hardware.l2_cache_size << '\n'
              << "l3_cache_size=" << hardware.l3_cache_size << '\n'
              << "cache_info_available=" << hardware.cache_info_available << '\n'
              << "numa_info_available=" << hardware.numa_info_available << '\n'
              << "page_size_available=" << hardware.page_size_available << '\n';
    return 0;
}
