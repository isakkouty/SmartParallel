#pragma once

#include <smart/data/view.hpp>
#include <smart/vision/image_view.hpp>

#include <cstddef>
#include <stdexcept>
#include <limits>

namespace smart::vision
{
template <typename T>
smart::data::MatrixView<T> as_element_matrix_view(const ImageView<T>& image)
{
    if (image.stride_bytes % sizeof(T) != 0)
        throw std::invalid_argument("SmartParallel ImageView stride is not element aligned");
    const auto logical_width = smart::saturating_multiply(image.width, image.channels);
    if (logical_width.saturated)
        throw std::overflow_error("SmartParallel ImageView logical width overflow");
    return smart::data::MatrixView<T>(
        image.data,
        {image.height, logical_width.value},
        {image.stride_bytes / sizeof(T), 1});
}

template <typename T>
ImageView<T> as_image_view(const smart::data::MatrixView<T>& matrix,
                           std::size_t width,
                           std::size_t channels)
{
    const auto logical_width = smart::saturating_multiply(width, channels);
    const auto stride_bytes = smart::saturating_multiply(matrix.stride(0), sizeof(T));
    if (channels == 0 || logical_width.saturated || stride_bytes.saturated
        || logical_width.value != matrix.extent(1) || matrix.stride(1) != 1
        || !matrix.has_unique_mapping()
        || (!matrix.empty() && matrix.stride(0) < matrix.extent(1)))
        throw std::invalid_argument("SmartParallel MatrixView cannot be represented as ImageView");
    return ImageView<T>{matrix.data(), width, matrix.extent(0),
                        stride_bytes.value, channels};
}
} // namespace smart::vision
