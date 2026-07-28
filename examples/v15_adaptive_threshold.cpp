#include <smart/vision/vision.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

int main()
{
    constexpr std::size_t width = 1920;
    constexpr std::size_t height = 1080;

    std::vector<std::uint8_t> source(width * height);
    std::vector<std::uint8_t> destination(width * height);

    for (std::size_t i = 0; i < source.size(); ++i)
        source[i] = static_cast<std::uint8_t>((i * 37U + 11U) & 0xffU);

    const auto source_view = smart::vision::make_contiguous_image_view(
        static_cast<const std::uint8_t*>(source.data()), width, height);
    const auto destination_view = smart::vision::make_contiguous_image_view(
        destination.data(), width, height);

    std::size_t stable_calls = 0;
    for (std::size_t call = 0; call < 128 && stable_calls < 8; ++call)
    {
        smart::vision::threshold(
            source_view,
            destination_view,
            smart::vision::ThresholdOptions{
                127,
                255,
                smart::vision::ThresholdMode::Binary,
            });

        const auto current = smart::vision::last_decision_report();
        const bool clean_learned_call = current.learned_route
            && !current.exploration_probe
            && !current.holdout_probe
            && !current.revalidation_probe
            && !current.drift_probe;
        stable_calls = clean_learned_call ? stable_calls + 1 : 0;
    }

    const auto decision = smart::vision::last_decision_report();
    const auto white_pixels = std::count(destination.begin(), destination.end(), std::uint8_t{255});

    std::cout << "Selected route: "
              << smart::vision::execution_route_name(decision.selected_route) << '\n';
    std::cout << "Learned route: " << (decision.learned_route ? "yes" : "no") << '\n';
    std::cout << "OpenCV available: " << (decision.opencv_available ? "yes" : "no") << '\n';
    std::cout << "White pixels: " << white_pixels << '\n';

    return decision.backend_authenticated ? 0 : 1;
}
