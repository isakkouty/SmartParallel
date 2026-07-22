#include <smart/hardware/hardware.hpp>
#include <smart/hardware/hardware_characteristics.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#elif defined(__linux__)
#  include <filesystem>
#  include <sched.h>
#  include <unistd.h>
#elif defined(__APPLE__)
#  include <sys/sysctl.h>
#  include <unistd.h>
#else
#  if defined(__unix__)
#    include <unistd.h>
#  endif
#endif

namespace smart
{
namespace
{
std::size_t standard_hardware_threads()
{
    const unsigned int count = std::thread::hardware_concurrency();
    return count == 0 ? 1 : static_cast<std::size_t>(count);
}

#if defined(__linux__)
namespace fs = std::filesystem;

bool all_digits(const std::string& value)
{
    return !value.empty()
        && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool read_text_file(const fs::path& path, std::string& value)
{
    std::ifstream input(path);
    if (!input)
        return false;
    std::getline(input, value);
    return static_cast<bool>(input) || !value.empty();
}

bool read_integer_file(const fs::path& path, long long& value)
{
    std::ifstream input(path);
    return static_cast<bool>(input >> value);
}

std::vector<int> linux_affinity_cpus()
{
    std::vector<int> cpus;
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0)
    {
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
        {
            if (CPU_ISSET(cpu, &set))
                cpus.push_back(cpu);
        }
    }
    return cpus;
}

bool linux_cpu_online(int cpu)
{
    if (cpu == 0)
        return true;
    long long online = 1;
    const fs::path path = fs::path("/sys/devices/system/cpu") / ("cpu" + std::to_string(cpu))
        / "online";
    return !fs::exists(path) || (read_integer_file(path, online) && online != 0);
}

std::vector<int> linux_online_cpus()
{
    std::vector<int> cpus = linux_affinity_cpus();
    if (!cpus.empty())
    {
        cpus.erase(std::remove_if(cpus.begin(), cpus.end(), [](int cpu) {
                       return !linux_cpu_online(cpu);
                   }),
                   cpus.end());
        if (!cpus.empty())
            return cpus;
    }

    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator("/sys/devices/system/cpu", error))
    {
        if (error || !entry.is_directory(error))
            continue;
        const std::string name = entry.path().filename().string();
        if (name.size() <= 3 || name.compare(0, 3, "cpu") != 0 || !all_digits(name.substr(3)))
            continue;
        const int cpu = std::stoi(name.substr(3));
        if (linux_cpu_online(cpu))
            cpus.push_back(cpu);
    }
    std::sort(cpus.begin(), cpus.end());
    cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
    return cpus;
}

std::size_t parse_size_bytes(std::string text)
{
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) {
                   return std::isspace(c) != 0;
               }),
               text.end());
    if (text.empty())
        return 0;

    std::size_t end = 0;
    unsigned long long value = 0;
    try
    {
        value = std::stoull(text, &end, 10);
    }
    catch (...)
    {
        return 0;
    }

    unsigned long long multiplier = 1;
    if (end < text.size())
    {
        const char suffix = static_cast<char>(std::toupper(static_cast<unsigned char>(text[end])));
        if (suffix == 'K')
            multiplier = 1024ull;
        else if (suffix == 'M')
            multiplier = 1024ull * 1024ull;
        else if (suffix == 'G')
            multiplier = 1024ull * 1024ull * 1024ull;
    }
    if (value > std::numeric_limits<std::size_t>::max() / multiplier)
        return 0;
    return static_cast<std::size_t>(value * multiplier);
}

std::size_t count_id_list(const std::string& text)
{
    std::size_t count = 0;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ','))
    {
        const std::size_t dash = token.find('-');
        try
        {
            if (dash == std::string::npos)
            {
                (void)std::stoull(token);
                ++count;
            }
            else
            {
                const unsigned long long first = std::stoull(token.substr(0, dash));
                const unsigned long long last = std::stoull(token.substr(dash + 1));
                if (last >= first)
                    count += static_cast<std::size_t>(last - first + 1);
            }
        }
        catch (...)
        {
            return 0;
        }
    }
    return count;
}

bool id_list_intersects(const std::string& text, const std::set<int>& selected)
{
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ','))
    {
        const std::size_t dash = token.find('-');
        try
        {
            const int first = std::stoi(token.substr(0, dash));
            const int last = dash == std::string::npos ? first : std::stoi(token.substr(dash + 1));
            if (last < first)
                continue;
            auto candidate = selected.lower_bound(first);
            if (candidate != selected.end() && *candidate <= last)
                return true;
        }
        catch (...)
        {
            return false;
        }
    }
    return false;
}

void discover_linux_cores(HardwareCharacteristics& hw, const std::vector<int>& cpus)
{
    std::set<std::tuple<long long, long long, long long>> cores;
    for (int cpu : cpus)
    {
        const fs::path topology = fs::path("/sys/devices/system/cpu")
            / ("cpu" + std::to_string(cpu)) / "topology";
        long long package = 0;
        long long die = 0;
        long long core = -1;
        (void)read_integer_file(topology / "physical_package_id", package);
        (void)read_integer_file(topology / "die_id", die);
        if (read_integer_file(topology / "core_id", core) && core >= 0)
            cores.emplace(package, die, core);
    }

    if (cores.empty())
    {
        const std::set<int> allowed(cpus.begin(), cpus.end());
        std::ifstream input("/proc/cpuinfo");
        std::string line;
        int processor = -1;
        long long package = 0;
        long long core = -1;
        auto commit = [&]() {
            if (processor >= 0 && core >= 0
                && (allowed.empty() || allowed.find(processor) != allowed.end()))
                cores.emplace(package, 0, core);
            processor = -1;
            package = 0;
            core = -1;
        };
        while (std::getline(input, line))
        {
            if (line.empty())
            {
                commit();
                continue;
            }
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) {
                          return std::isspace(c) != 0;
                      }),
                      key.end());
            value.erase(0, value.find_first_not_of(" \t"));
            try
            {
                if (key == "processor")
                    processor = std::stoi(value);
                else if (key == "physicalid")
                    package = std::stoll(value);
                else if (key == "coreid")
                    core = std::stoll(value);
            }
            catch (...)
            {
            }
        }
        commit();
    }

    if (!cores.empty())
        hw.physical_cores = std::min(hw.logical_threads, cores.size());
}

void discover_linux_numa(HardwareCharacteristics& hw, const std::vector<int>& cpus)
{
    const std::set<int> allowed(cpus.begin(), cpus.end());
    std::error_code error;
    std::size_t nodes = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator("/sys/devices/system/node", error))
    {
        if (error || !entry.is_directory(error))
            continue;
        const std::string name = entry.path().filename().string();
        if (name.size() <= 4 || name.compare(0, 4, "node") != 0 || !all_digits(name.substr(4)))
            continue;
        std::string cpulist;
        if (allowed.empty() || !read_text_file(entry.path() / "cpulist", cpulist)
            || id_list_intersects(cpulist, allowed))
            ++nodes;
    }

    if (nodes == 0 && allowed.empty())
    {
        std::string online;
        if (read_text_file("/sys/devices/system/node/online", online))
            nodes = count_id_list(online);
    }

    if (nodes > 0)
    {
        hw.numa_nodes = nodes;
        hw.numa_info_available = true;
    }
}

void discover_linux_caches(HardwareCharacteristics& hw, const std::vector<int>& cpus)
{
    std::set<std::tuple<int, std::string, std::string, std::size_t>> seen;
    for (int cpu : cpus)
    {
        const fs::path cache_root = fs::path("/sys/devices/system/cpu")
            / ("cpu" + std::to_string(cpu)) / "cache";
        std::error_code error;
        for (const fs::directory_entry& entry : fs::directory_iterator(cache_root, error))
        {
            if (error || !entry.is_directory(error))
                continue;
            const std::string index_name = entry.path().filename().string();
            if (index_name.size() <= 5 || index_name.compare(0, 5, "index") != 0)
                continue;

            long long level_value = 0;
            long long line_value = 0;
            std::string type;
            std::string size_text;
            std::string shared;
            if (!read_integer_file(entry.path() / "level", level_value)
                || !read_text_file(entry.path() / "type", type)
                || !read_text_file(entry.path() / "size", size_text))
                continue;
            (void)read_text_file(entry.path() / "shared_cpu_list", shared);
            (void)read_integer_file(entry.path() / "coherency_line_size", line_value);

            const std::size_t size = parse_size_bytes(size_text);
            if (level_value < 1 || level_value > 3 || size == 0)
                continue;
            if (shared.empty())
                shared = std::to_string(cpu);

            const auto key = std::make_tuple(static_cast<int>(level_value), type, shared, size);
            if (!seen.insert(key).second)
                continue;

            if (level_value == 1)
                hw.l1_cache_size += size;
            else if (level_value == 2)
                hw.l2_cache_size += size;
            else
                hw.l3_cache_size += size;

            if (line_value > 0)
                hw.cache_line_size = static_cast<std::size_t>(line_value);
            hw.cache_info_available = true;
        }
    }
}

HardwareCharacteristics discover_linux_hardware()
{
    HardwareCharacteristics hw;
    const std::vector<int> cpus = linux_online_cpus();
    hw.logical_threads = cpus.empty() ? standard_hardware_threads() : cpus.size();
    hw.physical_cores = hw.logical_threads;

    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size > 0)
    {
        hw.page_size = static_cast<std::size_t>(page_size);
        hw.page_size_available = true;
    }

    discover_linux_cores(hw, cpus);
    discover_linux_numa(hw, cpus);
    discover_linux_caches(hw, cpus);
    return hw;
}
#endif

#if defined(__APPLE__)
template <typename T>
bool macos_sysctl_value(const char* name, T& value)
{
    std::size_t size = sizeof(value);
    return sysctlbyname(name, &value, &size, nullptr, 0) == 0 && size > 0
        && size <= sizeof(value);
}

HardwareCharacteristics discover_macos_hardware()
{
    HardwareCharacteristics hw;

    std::uint64_t logical = 0;
    std::uint64_t physical = 0;
    std::uint64_t page = 0;
    std::uint64_t cache_line = 0;
    std::uint64_t l1_data = 0;
    std::uint64_t l1_instruction = 0;
    std::uint64_t l2 = 0;
    std::uint64_t l3 = 0;

    if (macos_sysctl_value("hw.logicalcpu", logical) && logical > 0)
        hw.logical_threads = static_cast<std::size_t>(logical);
    else
        hw.logical_threads = standard_hardware_threads();

    if (macos_sysctl_value("hw.physicalcpu", physical) && physical > 0)
        hw.physical_cores = std::min(hw.logical_threads, static_cast<std::size_t>(physical));
    else
        hw.physical_cores = hw.logical_threads;

    if (macos_sysctl_value("hw.pagesize", page) && page > 0)
    {
        hw.page_size = static_cast<std::size_t>(page);
        hw.page_size_available = true;
    }
    else
    {
        const long sysconf_page = sysconf(_SC_PAGESIZE);
        if (sysconf_page > 0)
        {
            hw.page_size = static_cast<std::size_t>(sysconf_page);
            hw.page_size_available = true;
        }
    }

    if (macos_sysctl_value("hw.cachelinesize", cache_line) && cache_line > 0)
        hw.cache_line_size = static_cast<std::size_t>(cache_line);

    const bool have_l1d = macos_sysctl_value("hw.l1dcachesize", l1_data) && l1_data > 0;
    const bool have_l1i = macos_sysctl_value("hw.l1icachesize", l1_instruction)
        && l1_instruction > 0;
    const bool have_l2 = macos_sysctl_value("hw.l2cachesize", l2) && l2 > 0;
    const bool have_l3 = macos_sysctl_value("hw.l3cachesize", l3) && l3 > 0;

    if (have_l1d)
        hw.l1_cache_size += static_cast<std::size_t>(l1_data);
    if (have_l1i)
        hw.l1_cache_size += static_cast<std::size_t>(l1_instruction);
    if (have_l2)
        hw.l2_cache_size = static_cast<std::size_t>(l2);
    if (have_l3)
        hw.l3_cache_size = static_cast<std::size_t>(l3);
    hw.cache_info_available = have_l1d || have_l1i || have_l2 || have_l3;

    // macOS does not expose a stable public NUMA topology API. Keep the
    // conservative one-node default and leave availability false rather than
    // guessing from CPU packages or Apple Silicon performance levels.
    return hw;
}
#endif

#if defined(_WIN32)
HardwareCharacteristics discover_windows_hardware()
{
    HardwareCharacteristics hw;
    hw.logical_threads = hardware_threads();
    hw.physical_cores = 0;
    hw.numa_nodes = 0;

    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    if (system_info.dwPageSize > 0)
    {
        hw.page_size = system_info.dwPageSize;
        hw.page_size_available = true;
    }

    DWORD length = 0;
    (void)GetLogicalProcessorInformationEx(RelationAll, nullptr, &length);
    if (length == 0)
    {
        hw.physical_cores = hw.logical_threads;
        hw.numa_nodes = 1;
        return hw;
    }

    std::vector<unsigned char> buffer(length);
    if (!GetLogicalProcessorInformationEx(
            RelationAll,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
            &length))
    {
        hw.physical_cores = hw.logical_threads;
        hw.numa_nodes = 1;
        return hw;
    }

    unsigned char* ptr = buffer.data();
    unsigned char* const end = buffer.data() + length;
    while (ptr < end)
    {
        auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
        if (info->Size == 0 || ptr + info->Size > end)
            break;

        if (info->Relationship == RelationProcessorCore)
        {
            ++hw.physical_cores;
        }
        else if (info->Relationship == RelationCache)
        {
            const CACHE_RELATIONSHIP& cache = info->Cache;
            hw.cache_info_available = true;
            if (cache.LineSize > 0)
                hw.cache_line_size = cache.LineSize;
            if (cache.Level == 1)
                hw.l1_cache_size += cache.CacheSize;
            else if (cache.Level == 2)
                hw.l2_cache_size += cache.CacheSize;
            else if (cache.Level == 3)
                hw.l3_cache_size += cache.CacheSize;
        }
        else if (info->Relationship == RelationNumaNode)
        {
            ++hw.numa_nodes;
            hw.numa_info_available = true;
        }
        ptr += info->Size;
    }

    if (hw.physical_cores == 0)
        hw.physical_cores = hw.logical_threads;
    if (hw.numa_nodes == 0)
        hw.numa_nodes = 1;
    return hw;
}
#endif
} // namespace

std::size_t hardware_threads()
{
#if defined(_WIN32)
    const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return count == 0 ? standard_hardware_threads() : static_cast<std::size_t>(count);
#elif defined(__linux__)
    const std::vector<int> cpus = linux_affinity_cpus();
    if (!cpus.empty())
        return cpus.size();
    const long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? static_cast<std::size_t>(count) : standard_hardware_threads();
#elif defined(__APPLE__)
    std::uint64_t count = 0;
    if (macos_sysctl_value("hw.logicalcpu", count) && count > 0)
        return static_cast<std::size_t>(count);
    const long online = sysconf(_SC_NPROCESSORS_ONLN);
    return online > 0 ? static_cast<std::size_t>(online) : standard_hardware_threads();
#elif defined(_SC_NPROCESSORS_ONLN)
    const long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? static_cast<std::size_t>(count) : standard_hardware_threads();
#else
    return standard_hardware_threads();
#endif
}

HardwareCharacteristics hardware_characteristics()
{
#if defined(_WIN32)
    // Preserve the established Windows discovery semantics.
    return discover_windows_hardware();
#elif defined(__linux__)
    // Native sysfs discovery can touch many small files. Hardware topology is
    // effectively immutable for the process, so discover it once.
    static const HardwareCharacteristics cached = discover_linux_hardware();
    return cached;
#elif defined(__APPLE__)
    static const HardwareCharacteristics cached = discover_macos_hardware();
    return cached;
#else
    HardwareCharacteristics hw;
    hw.logical_threads = hardware_threads();
    hw.physical_cores = hw.logical_threads;
#  if defined(_SC_PAGESIZE)
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size > 0)
    {
        hw.page_size = static_cast<std::size_t>(page_size);
        hw.page_size_available = true;
    }
#  endif
    return hw;
#endif
}
} // namespace smart
