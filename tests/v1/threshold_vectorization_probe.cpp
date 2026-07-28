#include <smart/vision/detail/threshold_kernel.hpp>

#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER)
#define SMARTPARALLEL_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define SMARTPARALLEL_NOINLINE __attribute__((noinline))
#else
#define SMARTPARALLEL_NOINLINE
#endif

namespace smartparallel_vectorization_probe
{
SMARTPARALLEL_NOINLINE void disjoint_binary(const std::uint8_t* source,
                                             std::uint8_t* destination,
                                             std::size_t count) noexcept
{
    smart::vision::detail::threshold_u8_contiguous_disjoint<false>(
        source, destination, count, 127, 255);
}

SMARTPARALLEL_NOINLINE void disjoint_inverse(const std::uint8_t* source,
                                              std::uint8_t* destination,
                                              std::size_t count) noexcept
{
    smart::vision::detail::threshold_u8_contiguous_disjoint<true>(
        source, destination, count, 127, 255);
}

SMARTPARALLEL_NOINLINE void in_place_binary(std::uint8_t* data,
                                            std::size_t count) noexcept
{
    smart::vision::detail::threshold_u8_contiguous_in_place<false>(
        data, count, 127, 255);
}
SMARTPARALLEL_NOINLINE void branchless_multiply_binary(
    const std::uint8_t* source, std::uint8_t* destination, std::size_t count) noexcept
{
    for (std::size_t index = 0; index < count; ++index)
        destination[index] = static_cast<std::uint8_t>(
            static_cast<unsigned>(source[index] > 127) * 255u);
}

SMARTPARALLEL_NOINLINE void branchless_mask_binary(
    const std::uint8_t* source, std::uint8_t* destination, std::size_t count) noexcept
{
    for (std::size_t index = 0; index < count; ++index)
    {
        const unsigned selected = static_cast<unsigned>(source[index] > 127);
        const unsigned mask = 0u - selected;
        destination[index] = static_cast<std::uint8_t>(255u & mask);
    }
}

} // namespace smartparallel_vectorization_probe

#undef SMARTPARALLEL_NOINLINE
