#include <smart/data/view.hpp>
#include <smart/execution/algorithms.hpp>
#include <smart/linalg/operations.hpp>
#include <smart/scientific/stencil.hpp>
#include <smart/execution/parallel.hpp>
#include <smart/hardware/hardware_characteristics.hpp>
#include <smart/version.hpp>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

int main()
{
    static_assert(SMARTPARALLEL_VERSION_MAJOR == 1, "unexpected major version");
    static_assert(SMARTPARALLEL_VERSION_MINOR == 6, "unexpected minor version");

    const smart::HardwareCharacteristics hardware = smart::hardware_characteristics();
    if (hardware.logical_threads == 0 || hardware.physical_cores == 0
        || hardware.physical_cores > hardware.logical_threads || hardware.page_size == 0)
    {
        std::cerr << "consumer validation failed: invalid hardware characteristics\n";
        return 1;
    }

    constexpr std::size_t count = 4096;
    std::vector<std::size_t> values(count, 0);
    std::atomic<std::size_t> visits{0};

    smart::parallel_for(
        std::size_t{0},
        count,
        [&](std::size_t i)
        {
            values[i] = i * 3 + 1;
            visits.fetch_add(1, std::memory_order_relaxed);
        });

    if (visits.load(std::memory_order_relaxed) != count)
    {
        std::cerr << "consumer validation failed: wrong visit count\n";
        return 1;
    }

    for (std::size_t i = 0; i < count; ++i)
    {
        if (values[i] != i * 3 + 1)
        {
            std::cerr << "consumer validation failed at index " << i << '\n';
            return 1;
        }
    }

    std::vector<std::size_t> transformed(count, 0);
    smart::parallel_transform(
        values.begin(), values.end(), transformed.begin(),
        [](std::size_t value) { return value + 2; });
    const std::size_t sum = smart::parallel_reduce(
        transformed.begin(), transformed.end(), std::size_t{0});
    std::size_t expected_sum = 0;
    for (std::size_t i = 0; i < count; ++i)
        expected_sum += i * 3 + 3;
    if (sum != expected_sum)
    {
        std::cerr << "consumer validation failed: v1.4 algorithm result mismatch\n";
        return 1;
    }

    std::vector<double> scientific_values{1.0e16, 1.0, -1.0e16};
    const auto scientific_view = smart::data::VectorView<const double>::contiguous(
        scientific_values.data(), {scientific_values.size()});
    const double accurate_sum = smart::parallel_reduce(
        scientific_values.begin(), scientific_values.end(), 0.0,
        smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    const double robust_norm = smart::linalg::norm(
        scientific_view, smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    if (accurate_sum != 1.0 || !std::isfinite(robust_norm))
    {
        std::cerr << "consumer validation failed: v1.6 scientific result mismatch\n";
        return 1;
    }

    std::cout << "SmartParallel " << SMARTPARALLEL_VERSION_STRING
              << " installed-package consumer passed\n";
    return 0;
}
