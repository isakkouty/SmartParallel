#include <smart/vision/vision.hpp>

#include <array>
#include <cstdint>

int main()
{
    const std::array<std::uint8_t, 4> source{0, 127, 128, 255};
    std::array<std::uint8_t, 4> destination{};
    smart::vision::threshold(
        smart::vision::make_contiguous_image_view(
            static_cast<const std::uint8_t*>(source.data()), source.size(), 1),
        smart::vision::make_contiguous_image_view(destination.data(), destination.size(), 1),
        {127, 255, smart::vision::ThresholdMode::Binary},
        {smart::vision::ExecutionRoute::NativeSequential, 1});
    return destination == std::array<std::uint8_t, 4>{0, 0, 255, 255} ? 0 : 1;
}
