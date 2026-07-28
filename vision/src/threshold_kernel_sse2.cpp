#include "threshold_kernel_internal.hpp"

#include <cstddef>
#include <cstdint>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <emmintrin.h>
#define SMARTPARALLEL_HAS_SSE2_SOURCE 1
#else
#define SMARTPARALLEL_HAS_SSE2_SOURCE 0
#endif

namespace smart::vision::detail
{
bool threshold_sse2_compiled() noexcept
{
    return SMARTPARALLEL_HAS_SSE2_SOURCE != 0;
}

#if SMARTPARALLEL_HAS_SSE2_SOURCE
namespace
{
template <bool Inverse>
void threshold_sse2(const std::uint8_t* source,
                    std::uint8_t* destination,
                    std::size_t count,
                    std::uint8_t threshold,
                    std::uint8_t maximum) noexcept
{
    constexpr std::size_t width = 16;
    const __m128i sign = _mm_set1_epi8(static_cast<char>(0x80));
    const __m128i threshold_value = _mm_set1_epi8(
        static_cast<char>(threshold ^ std::uint8_t{0x80}));
    const __m128i maximum_value = _mm_set1_epi8(static_cast<char>(maximum));
    std::size_t index = 0;
    for (; index + width <= count; index += width)
    {
        const __m128i values = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(source + index));
        const __m128i adjusted = _mm_xor_si128(values, sign);
        const __m128i mask = _mm_cmpgt_epi8(adjusted, threshold_value);
        const __m128i result = Inverse
            ? _mm_andnot_si128(mask, maximum_value)
            : _mm_and_si128(mask, maximum_value);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + index), result);
    }
    if constexpr (Inverse)
        threshold_scalar_inverse(source + index, destination + index,
                                 count - index, threshold, maximum);
    else
        threshold_scalar_binary(source + index, destination + index,
                                count - index, threshold, maximum);
}
} // namespace

void threshold_sse2_binary(const std::uint8_t* source,
                           std::uint8_t* destination,
                           std::size_t count,
                           std::uint8_t threshold,
                           std::uint8_t maximum) noexcept
{
    threshold_sse2<false>(source, destination, count, threshold, maximum);
}

void threshold_sse2_inverse(const std::uint8_t* source,
                            std::uint8_t* destination,
                            std::size_t count,
                            std::uint8_t threshold,
                            std::uint8_t maximum) noexcept
{
    threshold_sse2<true>(source, destination, count, threshold, maximum);
}
#else
void threshold_sse2_binary(const std::uint8_t* source,
                           std::uint8_t* destination,
                           std::size_t count,
                           std::uint8_t threshold,
                           std::uint8_t maximum) noexcept
{
    threshold_scalar_binary(source, destination, count, threshold, maximum);
}
void threshold_sse2_inverse(const std::uint8_t* source,
                            std::uint8_t* destination,
                            std::size_t count,
                            std::uint8_t threshold,
                            std::uint8_t maximum) noexcept
{
    threshold_scalar_inverse(source, destination, count, threshold, maximum);
}
#endif
} // namespace smart::vision::detail

#undef SMARTPARALLEL_HAS_SSE2_SOURCE
