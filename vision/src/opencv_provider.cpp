#include "opencv_provider.hpp"

#include <opencv2/core.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/imgproc.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace smart::vision::detail
{
namespace
{
std::once_flag provider_state_once;
std::atomic<std::size_t> provider_fingerprint{0};
std::atomic<std::uint64_t> provider_generation{0};

std::size_t compute_provider_fingerprint() noexcept
{
    try
    {
        auto combine = [](std::size_t seed, std::size_t value)
        {
            return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u));
        };
        std::size_t fingerprint = std::hash<std::string>{}(
            std::string("opencv:") + CV_VERSION + ":" + cv::getBuildInformation());
        fingerprint = combine(fingerprint, static_cast<std::size_t>(cv::getNumThreads()));
        fingerprint = combine(fingerprint, cv::useOptimized() ? 1u : 0u);
        fingerprint = combine(fingerprint, cv::ocl::useOpenCL() ? 1u : 0u);
        return fingerprint;
    }
    catch (...)
    {
        return std::hash<std::string>{}(std::string("opencv:") + CV_VERSION);
    }
}

void initialize_provider_state() noexcept
{
    provider_fingerprint.store(compute_provider_fingerprint(), std::memory_order_release);
    provider_generation.store(1, std::memory_order_release);
}
} // namespace

bool opencv_provider_available() noexcept { return true; }

std::string opencv_provider_version()
{
    return CV_VERSION;
}

OpenCvProviderState opencv_provider_state() noexcept
{
    std::call_once(provider_state_once, initialize_provider_state);
    return {provider_fingerprint.load(std::memory_order_acquire),
            provider_generation.load(std::memory_order_acquire)};
}

std::size_t opencv_provider_fingerprint() noexcept
{
    return opencv_provider_state().fingerprint;
}

void refresh_opencv_provider_state() noexcept
{
    std::call_once(provider_state_once, initialize_provider_state);
    provider_fingerprint.store(compute_provider_fingerprint(), std::memory_order_release);
    provider_generation.fetch_add(1, std::memory_order_acq_rel);
}

void execute_opencv_threshold(ImageView<const std::uint8_t> source,
                              ImageView<std::uint8_t> destination,
                              ThresholdOptions options)
{
    cv::Mat source_mat(static_cast<int>(source.height),
                       static_cast<int>(source.width),
                       CV_8UC1,
                       const_cast<std::uint8_t*>(source.data),
                       source.stride_bytes);
    cv::Mat destination_mat(static_cast<int>(destination.height),
                            static_cast<int>(destination.width),
                            CV_8UC1,
                            destination.data,
                            destination.stride_bytes);
    const int mode = options.mode == ThresholdMode::Binary
        ? cv::THRESH_BINARY
        : cv::THRESH_BINARY_INV;
    cv::threshold(source_mat,
                  destination_mat,
                  static_cast<double>(options.threshold),
                  static_cast<double>(options.maximum_value),
                  mode);
}
} // namespace smart::vision::detail
