#pragma once

#include <smart/vision/decision_report.hpp>
#include <smart/vision/image_view.hpp>
#include <smart/execution/execution_context.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace smart::vision
{
enum class ThresholdMode
{
    Binary,
    BinaryInverse
};

struct ThresholdOptions
{
    std::uint8_t threshold = 127;
    std::uint8_t maximum_value = 255;
    ThresholdMode mode = ThresholdMode::Binary;
};

struct ExecutionPolicy
{
    ExecutionRoute route = ExecutionRoute::Auto;
    std::size_t worker_budget = 0;
};

void threshold(ImageView<const std::uint8_t> source,
               ImageView<std::uint8_t> destination,
               ThresholdOptions options = {},
               ExecutionPolicy policy = {});

void threshold(const ExecutionContext& context,
               ImageView<const std::uint8_t> source,
               ImageView<std::uint8_t> destination,
               ThresholdOptions options = {},
               ExecutionPolicy policy = {});

bool opencv_available() noexcept;
std::string opencv_version();
void refresh_provider_state() noexcept;
void clear_adaptive_route_cache() noexcept;
} // namespace smart::vision
