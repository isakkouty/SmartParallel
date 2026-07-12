#pragma once

#include <cstddef>

#include <smart/hardware/hardware.hpp>

#ifdef _WIN32
    #define NOMINMAX
    #include <windows.h>
    #include <vector>
#endif

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

    inline HardwareCharacteristics hardware_characteristics()
    {
        HardwareCharacteristics hw;
        hw.logical_threads = hardware_threads();

#ifdef _WIN32
        hw.physical_cores = 0;
        hw.numa_nodes = 0;

        SYSTEM_INFO system_info;
        GetSystemInfo(&system_info);

        hw.page_size = system_info.dwPageSize;
        hw.page_size_available = true;

        DWORD length = 0;

        GetLogicalProcessorInformationEx(
            RelationAll,
            nullptr,
            &length
        );

        if (length == 0)
        {
            hw.physical_cores = hw.logical_threads;
            hw.numa_nodes = 1;
            return hw;
        }

        std::vector<unsigned char> buffer(length);

        if (!GetLogicalProcessorInformationEx(
                RelationAll,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                    buffer.data()
                ),
                &length))
        {
            hw.physical_cores = hw.logical_threads;
            hw.numa_nodes = 1;
            return hw;
        }

        unsigned char* ptr = buffer.data();
        unsigned char* end = buffer.data() + length;

        while (ptr < end)
        {
            auto* info =
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                    ptr
                );

            if (info->Relationship == RelationProcessorCore)
            {
                ++hw.physical_cores;
            }
            else if (info->Relationship == RelationCache)
            {
                const CACHE_RELATIONSHIP& cache = info->Cache;

                hw.cache_info_available = true;
                hw.cache_line_size = cache.LineSize;

                if (cache.Level == 1)
                {
                    hw.l1_cache_size += cache.CacheSize;
                }
                else if (cache.Level == 2)
                {
                    hw.l2_cache_size += cache.CacheSize;
                }
                else if (cache.Level == 3)
                {
                    hw.l3_cache_size += cache.CacheSize;
                }
            }
            else if (info->Relationship == RelationNumaNode)
            {
                ++hw.numa_nodes;
                hw.numa_info_available = true;
            }

            ptr += info->Size;
        }

        if (hw.physical_cores == 0)
        {
            hw.physical_cores = hw.logical_threads;
        }

        if (hw.numa_nodes == 0)
        {
            hw.numa_nodes = 1;
        }
#endif

        return hw;
    }
}
