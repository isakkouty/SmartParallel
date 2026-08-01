#pragma once

#include <smart/execution/algorithms.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>

namespace smart::detail
{
inline constexpr std::size_t canonical_pointwise_target_elements = 4096;
inline constexpr const char* canonical_pointwise_plan_v1 =
    "canonical-pointwise-v1-target4096";
inline constexpr const char* canonical_pointwise_2d_plan_v1 =
    "canonical-pointwise-2d-v1-target4096";

inline std::size_t fixed_chunk_count(std::size_t total, std::size_t grain) noexcept
{
    if (total == 0)
        return 0;
    return 1 + (total - 1) / std::max<std::size_t>(std::size_t{1}, grain);
}

inline AlgorithmChunkRange fixed_chunk_range(std::size_t total,
                                             std::size_t grain,
                                             std::size_t ordinal) noexcept
{
    const std::size_t safe_grain = std::max<std::size_t>(std::size_t{1}, grain);
    const std::size_t begin = ordinal > std::numeric_limits<std::size_t>::max() / safe_grain
        ? total
        : std::min(total, ordinal * safe_grain);
    const std::size_t remaining = total - begin;
    return {begin, remaining <= safe_grain ? total : begin + safe_grain};
}

template <typename RangeFunction>
void execute_canonical_pointwise(std::size_t total,
                                 std::size_t callsite,
                                 RangeFunction&& range_function)
{
    const std::size_t chunks = fixed_chunk_count(total, canonical_pointwise_target_elements);
    if (chunks == 0)
        return;
    auto chunk = [&](std::size_t ordinal)
    {
        range_function(fixed_chunk_range(
            total, canonical_pointwise_target_elements, ordinal));
    };
    auto sequential = [&](std::size_t begin, std::size_t end)
    {
        for (std::size_t ordinal = begin; ordinal < end; ++ordinal)
            chunk(ordinal);
    };
    execute_algorithm_chunks(
        chunks,
        callsite,
        AlgorithmChunkFunctionRef(chunk),
        AlgorithmSequentialRangeFunctionRef(sequential));
}

template <typename RowFunction>
void execute_canonical_pointwise_rows(std::size_t rows,
                                      std::size_t columns,
                                      std::size_t callsite,
                                      RowFunction&& row_function)
{
    if (rows == 0)
        return;
    const std::size_t rows_per_chunk = columns == 0
        ? std::size_t{1}
        : std::max<std::size_t>(
            std::size_t{1}, canonical_pointwise_target_elements / columns);
    const std::size_t chunks = fixed_chunk_count(rows, rows_per_chunk);
    auto chunk = [&](std::size_t ordinal)
    {
        const auto range = fixed_chunk_range(rows, rows_per_chunk, ordinal);
        for (std::size_t row = range.begin; row < range.end; ++row)
            row_function(row);
    };
    auto sequential = [&](std::size_t begin, std::size_t end)
    {
        for (std::size_t ordinal = begin; ordinal < end; ++ordinal)
            chunk(ordinal);
    };
    execute_algorithm_chunks(
        chunks,
        callsite,
        AlgorithmChunkFunctionRef(chunk),
        AlgorithmSequentialRangeFunctionRef(sequential));
}
} // namespace smart::detail
