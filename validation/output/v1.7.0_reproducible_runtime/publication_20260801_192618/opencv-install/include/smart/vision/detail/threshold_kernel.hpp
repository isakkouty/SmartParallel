#pragma once

#include <cstddef>
#include <cstdint>

namespace smart::vision::detail
{
enum class ThresholdKernelKind
{
    Scalar,
    SSE2,
    AVX2
};

ThresholdKernelKind selected_threshold_kernel() noexcept;
const char* threshold_kernel_name(ThresholdKernelKind kind) noexcept;
bool threshold_kernel_uses_explicit_simd() noexcept;

void threshold_u8_contiguous_disjoint_binary(const std::uint8_t* source,
                                              std::uint8_t* destination,
                                              std::size_t count,
                                              std::uint8_t threshold,
                                              std::uint8_t maximum) noexcept;
void threshold_u8_contiguous_disjoint_inverse(const std::uint8_t* source,
                                               std::uint8_t* destination,
                                               std::size_t count,
                                               std::uint8_t threshold,
                                               std::uint8_t maximum) noexcept;
void threshold_u8_contiguous_in_place_binary(std::uint8_t* data,
                                             std::size_t count,
                                             std::uint8_t threshold,
                                             std::uint8_t maximum) noexcept;
void threshold_u8_contiguous_in_place_inverse(std::uint8_t* data,
                                              std::size_t count,
                                              std::uint8_t threshold,
                                              std::uint8_t maximum) noexcept;

template <bool Inverse>
inline void threshold_u8_contiguous_disjoint(const std::uint8_t* source,
                                             std::uint8_t* destination,
                                             std::size_t count,
                                             std::uint8_t threshold,
                                             std::uint8_t maximum) noexcept
{
    if constexpr (Inverse)
        threshold_u8_contiguous_disjoint_inverse(
            source, destination, count, threshold, maximum);
    else
        threshold_u8_contiguous_disjoint_binary(
            source, destination, count, threshold, maximum);
}

template <bool Inverse>
inline void threshold_u8_contiguous_in_place(std::uint8_t* data,
                                             std::size_t count,
                                             std::uint8_t threshold,
                                             std::uint8_t maximum) noexcept
{
    if constexpr (Inverse)
        threshold_u8_contiguous_in_place_inverse(data, count, threshold, maximum);
    else
        threshold_u8_contiguous_in_place_binary(data, count, threshold, maximum);
}

template <bool Inverse>
inline void threshold_u8_rows_disjoint(const std::uint8_t* source,
                                       std::size_t source_stride,
                                       std::uint8_t* destination,
                                       std::size_t destination_stride,
                                       std::size_t width,
                                       std::size_t row_begin,
                                       std::size_t row_end,
                                       std::uint8_t threshold,
                                       std::uint8_t maximum) noexcept
{
    for (std::size_t row = row_begin; row < row_end; ++row)
    {
        threshold_u8_contiguous_disjoint<Inverse>(
            source + row * source_stride,
            destination + row * destination_stride,
            width,
            threshold,
            maximum);
    }
}

template <bool Inverse>
inline void threshold_u8_rows_in_place(std::uint8_t* data,
                                       std::size_t stride,
                                       std::size_t width,
                                       std::size_t row_begin,
                                       std::size_t row_end,
                                       std::uint8_t threshold,
                                       std::uint8_t maximum) noexcept
{
    for (std::size_t row = row_begin; row < row_end; ++row)
    {
        threshold_u8_contiguous_in_place<Inverse>(
            data + row * stride, width, threshold, maximum);
    }
}

template <bool Inverse>
inline void threshold_u8_range(const std::uint8_t* source,
                               std::uint8_t* destination,
                               std::size_t begin,
                               std::size_t end,
                               std::uint8_t threshold,
                               std::uint8_t maximum) noexcept
{
    const std::size_t count = end - begin;
    if (source == destination)
    {
        threshold_u8_contiguous_in_place<Inverse>(
            destination + begin, count, threshold, maximum);
    }
    else
    {
        threshold_u8_contiguous_disjoint<Inverse>(
            source + begin, destination + begin, count, threshold, maximum);
    }
}

template <bool Inverse>
inline void threshold_u8_rows(const std::uint8_t* source,
                              std::size_t source_stride,
                              std::uint8_t* destination,
                              std::size_t destination_stride,
                              std::size_t width,
                              std::size_t row_begin,
                              std::size_t row_end,
                              std::uint8_t threshold,
                              std::uint8_t maximum) noexcept
{
    if (source == destination)
    {
        threshold_u8_rows_in_place<Inverse>(
            destination, destination_stride, width, row_begin, row_end,
            threshold, maximum);
    }
    else
    {
        threshold_u8_rows_disjoint<Inverse>(
            source, source_stride, destination, destination_stride, width,
            row_begin, row_end, threshold, maximum);
    }
}

inline void threshold_u8_range(const std::uint8_t* source,
                               std::uint8_t* destination,
                               std::size_t begin,
                               std::size_t end,
                               std::uint8_t threshold,
                               std::uint8_t maximum,
                               bool inverse) noexcept
{
    if (inverse)
        threshold_u8_range<true>(source, destination, begin, end, threshold, maximum);
    else
        threshold_u8_range<false>(source, destination, begin, end, threshold, maximum);
}

inline void threshold_u8_rows(const std::uint8_t* source,
                              std::size_t source_stride,
                              std::uint8_t* destination,
                              std::size_t destination_stride,
                              std::size_t width,
                              std::size_t row_begin,
                              std::size_t row_end,
                              std::uint8_t threshold,
                              std::uint8_t maximum,
                              bool inverse) noexcept
{
    if (inverse)
    {
        threshold_u8_rows<true>(source, source_stride, destination,
                                destination_stride, width, row_begin, row_end,
                                threshold, maximum);
    }
    else
    {
        threshold_u8_rows<false>(source, source_stride, destination,
                                 destination_stride, width, row_begin, row_end,
                                 threshold, maximum);
    }
}
} // namespace smart::vision::detail
