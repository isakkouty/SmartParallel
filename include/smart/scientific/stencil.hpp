#pragma once

#include <smart/data/view.hpp>
#include <smart/execution/algorithms.hpp>
#include <smart/numerical/detail/canonical_pointwise.hpp>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace smart::scientific
{
template <typename T>
struct Stencil2DCoefficients
{
    T center{};
    T north{};
    T south{};
    T west{};
    T east{};
};

template <typename T>
void stencil_2d(smart::data::MatrixView<const T> input,
                smart::data::MatrixView<T> output,
                const Stencil2DCoefficients<T>& coefficients,
                NumericalOptions options = {})
{
    static_assert(std::is_floating_point_v<T>,
                  "SmartParallel stencil_2d supports floating point");
    smart::detail::require_numerical_capability(
        smart::detail::native_pointwise_capabilities, options.policy);
    if (input.extents() != output.extents())
        throw std::invalid_argument("SmartParallel stencil_2d requires matching dimensions");
    if (!output.has_unique_mapping())
        throw std::invalid_argument(
            "SmartParallel stencil_2d requires uniquely mapped output elements");
    const auto overlap = output.overlap(input);
    if (overlap != smart::data::OverlapKind::Disjoint)
        throw std::invalid_argument("SmartParallel stencil_2d requires disjoint input and output");

    const std::size_t rows = input.extent(0);
    const std::size_t columns = input.extent(1);
    if (rows == 0 || columns == 0)
    {
        smart::detail::authenticate_numerical_execution(
            "stencil_2d", options.policy,
            options.policy == NumericalPolicy::Fast
                ? smart::detail::AccumulationMethod::Native
                : smart::detail::AccumulationMethod::FixedPointwiseExpression,
            options.policy == NumericalPolicy::Fast
                ? "none" : smart::detail::canonical_pointwise_2d_plan_v1,
            true, false);
        return;
    }

    const T* const input_data = input.data();
    T* const output_data = output.data();
    const std::size_t input_row_stride = input.stride(0);
    const std::size_t input_column_stride = input.stride(1);
    const std::size_t output_row_stride = output.stride(0);
    const std::size_t output_column_stride = output.stride(1);

    auto process_row = [=, &coefficients](std::size_t row)
    {
        const T* const input_row = input_data + row * input_row_stride;
        T* const output_row = output_data + row * output_row_stride;
        if (row == 0 || row + 1 == rows || columns < 3)
        {
            if (input_column_stride == 1 && output_column_stride == 1)
            {
                std::copy_n(input_row, columns, output_row);
            }
            else
            {
                const T* input_pointer = input_row;
                T* output_pointer = output_row;
                for (std::size_t column = 0; column < columns; ++column)
                {
                    *output_pointer = *input_pointer;
                    input_pointer += input_column_stride;
                    output_pointer += output_column_stride;
                }
            }
            return;
        }

        output_row[0] = input_row[0];
        const T* const north_row = input_data + (row - 1) * input_row_stride;
        const T* const south_row = input_data + (row + 1) * input_row_stride;
        if (input_column_stride == 1 && output_column_stride == 1)
        {
            for (std::size_t column = 1; column + 1 < columns; ++column)
            {
                // The expression order is part of stencil-2d-v1. Accurate currently
                // shares this pointwise arithmetic contract with Reproducible.
                T value = coefficients.center * input_row[column];
                value += coefficients.north * north_row[column];
                value += coefficients.south * south_row[column];
                value += coefficients.west * input_row[column - 1];
                value += coefficients.east * input_row[column + 1];
                output_row[column] = value;
            }
            output_row[columns - 1] = input_row[columns - 1];
            return;
        }

        for (std::size_t column = 1; column + 1 < columns; ++column)
        {
            const std::size_t offset = column * input_column_stride;
            T value = coefficients.center * input_row[offset];
            value += coefficients.north * north_row[offset];
            value += coefficients.south * south_row[offset];
            value += coefficients.west * input_row[offset - input_column_stride];
            value += coefficients.east * input_row[offset + input_column_stride];
            output_row[column * output_column_stride] = value;
        }
        output_row[(columns - 1) * output_column_stride] =
            input_row[(columns - 1) * input_column_stride];
    };

    if (options.policy == NumericalPolicy::Fast)
    {
        smart::detail::run_chunked_algorithm(
            rows, 0x57E2D000u,
            [&](smart::detail::AlgorithmChunkRange range)
            {
                for (std::size_t row = range.begin; row < range.end; ++row)
                    process_row(row);
            },
            [&]
            {
                for (std::size_t row = 0; row < rows; ++row)
                    process_row(row);
            });
        smart::detail::authenticate_numerical_execution(
            "stencil_2d", options.policy, smart::detail::AccumulationMethod::Native, "none");
    }
    else
    {
        smart::detail::execute_canonical_pointwise_rows(
            rows, columns, 0x57E2D001u, process_row);
        smart::detail::authenticate_numerical_execution(
            "stencil_2d", options.policy,
            smart::detail::AccumulationMethod::FixedPointwiseExpression,
            smart::detail::canonical_pointwise_2d_plan_v1);
    }
}
} // namespace smart::scientific
