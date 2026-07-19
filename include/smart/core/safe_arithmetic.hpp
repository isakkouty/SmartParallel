#pragma once

#include <cstddef>
#include <limits>

namespace smart
{
struct SizeCalculation
{
    std::size_t value = 0;
    bool saturated = false;
};

inline SizeCalculation saturating_add(std::size_t left, std::size_t right)
{
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();

    if (right > maximum - left)
    {
        return SizeCalculation{maximum, true};
    }

    return SizeCalculation{left + right, false};
}

inline SizeCalculation saturating_multiply(std::size_t left, std::size_t right)
{
    if (left == 0 || right == 0)
    {
        return SizeCalculation{0, false};
    }

    const std::size_t maximum = std::numeric_limits<std::size_t>::max();

    if (left > maximum / right)
    {
        return SizeCalculation{maximum, true};
    }

    return SizeCalculation{left * right, false};
}
} // namespace smart
