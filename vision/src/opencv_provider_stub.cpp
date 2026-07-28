#include "opencv_provider.hpp"

#include <stdexcept>

namespace smart::vision::detail
{
bool opencv_provider_available() noexcept { return false; }
std::string opencv_provider_version() { return "unavailable"; }
OpenCvProviderState opencv_provider_state() noexcept { return {0, 1}; }
std::size_t opencv_provider_fingerprint() noexcept { return 0; }
void refresh_opencv_provider_state() noexcept {}

void execute_opencv_threshold(ImageView<const std::uint8_t>,
                              ImageView<std::uint8_t>,
                              ThresholdOptions)
{
    throw std::runtime_error("SmartParallel OpenCV vision route is unavailable");
}
} // namespace smart::vision::detail
