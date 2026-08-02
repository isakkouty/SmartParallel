#include "opencv_provider.hpp"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void invoke_provider()
{
    constexpr std::size_t width = 128;
    constexpr std::size_t height = 96;
    std::vector<std::uint8_t> source(width * height, 0);
    std::vector<std::uint8_t> destination(width * height, 0);
    for (std::size_t index = 0; index < source.size(); ++index)
        source[index] = static_cast<std::uint8_t>((index * 37u) & 255u);
    smart::vision::ImageView<const std::uint8_t> input{
        source.data(), width, height, width, 1};
    smart::vision::ImageView<std::uint8_t> output{
        destination.data(), width, height, width, 1};
    smart::vision::detail::execute_opencv_threshold(
        input, output,
        smart::vision::ThresholdOptions{111, 255, smart::vision::ThresholdMode::Binary});
    for (std::size_t index = 0; index < source.size(); ++index)
        require(destination[index] == (source[index] > 111 ? 255 : 0),
                "contained OpenCV output mismatch");
}
}

int main()
{
    try
    {
        require(smart::vision::detail::opencv_provider_available(),
                "OpenCV provider unexpectedly unavailable");
        const int previous_threads = cv::getNumThreads();
        cv::setNumThreads(std::max(2, previous_threads));
        const int configured_threads = cv::getNumThreads();
        const auto before = smart::vision::detail::opencv_containment_snapshot();
        std::thread first(invoke_provider);
        std::thread second(invoke_provider);
        first.join();
        second.join();
        const auto after = smart::vision::detail::opencv_containment_snapshot();
        require(cv::getNumThreads() == configured_threads,
                "OpenCV thread state was not restored");
        require(after.contained_calls >= before.contained_calls + 2,
                "OpenCV containment calls were not recorded");
        require(after.restoration_failures == before.restoration_failures,
                "OpenCV containment restoration failed");
        require(after.maximum_concurrent_invocations == 1,
                "OpenCV provider invocation was not serialized");
        cv::setNumThreads(previous_threads);
        std::cout << "SmartParallel v1.8 OpenCV containment validation passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SmartParallel v1.8 OpenCV containment validation failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
