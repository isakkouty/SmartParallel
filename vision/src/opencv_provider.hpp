#pragma once

#include <smart/vision/image_view.hpp>
#include <smart/vision/threshold.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace smart::vision::detail
{
struct OpenCvProviderState
{
    std::size_t fingerprint = 0;
    std::uint64_t generation = 0;
};

bool opencv_provider_available() noexcept;
std::string opencv_provider_version();
OpenCvProviderState opencv_provider_state() noexcept;
std::size_t opencv_provider_fingerprint() noexcept;
void refresh_opencv_provider_state() noexcept;
void execute_opencv_threshold(ImageView<const std::uint8_t> source,
                              ImageView<std::uint8_t> destination,
                              ThresholdOptions options);
} // namespace smart::vision::detail
