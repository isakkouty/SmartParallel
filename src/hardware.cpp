#include <smart/hardware/hardware.hpp>
#include <thread>

namespace smart
{
std::size_t hardware_threads()
{
    unsigned int count = std::thread::hardware_concurrency();

    if (count == 0)
        return 1;

    return static_cast<std::size_t>(count);
}
} // namespace smart
