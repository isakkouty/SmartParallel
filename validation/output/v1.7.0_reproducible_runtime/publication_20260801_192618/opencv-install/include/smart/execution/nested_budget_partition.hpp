#pragma once

#include <algorithm>
#include <cstddef>

namespace smart
{
struct NestedBudgetPartition
{
    std::size_t child_index = 0;
    std::size_t child_count = 1;
    std::size_t parent_budget = 1;
    std::size_t allocated_budget = 1;

    bool exhausted() const noexcept
    {
        return allocated_budget == 0;
    }
};

class NestedBudgetPartitioner
{
  public:
    NestedBudgetPartition partition(std::size_t parent_budget,
                                    std::size_t child_count,
                                    std::size_t child_index) const noexcept
    {
        NestedBudgetPartition result;
        result.parent_budget = std::max<std::size_t>(1, parent_budget);
        result.child_count = std::max<std::size_t>(1, child_count);
        result.child_index = child_index;

        if (child_index >= result.child_count)
        {
            result.allocated_budget = 0;
            return result;
        }

        const std::size_t base = result.parent_budget / result.child_count;
        const std::size_t remainder = result.parent_budget % result.child_count;
        result.allocated_budget = base + (child_index < remainder ? 1 : 0);
        return result;
    }
};
} // namespace smart
