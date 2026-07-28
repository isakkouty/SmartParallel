#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace smart::vision
{
template <typename T>
struct ImageView
{
    using value_type = std::remove_const_t<T>;

    T* data = nullptr;
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t stride_bytes = 0;
    std::size_t channels = 1;

    bool empty() const noexcept { return width == 0 || height == 0; }

    std::size_t row_bytes() const noexcept
    {
        return width * channels * sizeof(value_type);
    }

    bool contiguous() const noexcept
    {
        return empty() || stride_bytes == row_bytes();
    }

    T* row(std::size_t y) const noexcept
    {
        auto* bytes = reinterpret_cast<std::conditional_t<std::is_const_v<T>,
                                                          const std::uint8_t*,
                                                          std::uint8_t*>>(data);
        return reinterpret_cast<T*>(bytes + y * stride_bytes);
    }
};

template <typename T>
ImageView<T> make_image_view(T* data,
                             std::size_t width,
                             std::size_t height,
                             std::size_t stride_bytes,
                             std::size_t channels = 1) noexcept
{
    return ImageView<T>{data, width, height, stride_bytes, channels};
}

template <typename T>
ImageView<T> make_contiguous_image_view(T* data,
                                        std::size_t width,
                                        std::size_t height,
                                        std::size_t channels = 1) noexcept
{
    return ImageView<T>{data,
                        width,
                        height,
                        width * channels * sizeof(std::remove_const_t<T>),
                        channels};
}
} // namespace smart::vision
