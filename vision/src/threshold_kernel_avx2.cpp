#include "threshold_kernel_internal.hpp"

#include <cstddef>
#include <cstdint>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define SMARTPARALLEL_HAS_AVX2_SOURCE 1
#else
#define SMARTPARALLEL_HAS_AVX2_SOURCE 0
#endif

namespace smart::vision::detail
{
bool threshold_avx2_compiled() noexcept
{
    return SMARTPARALLEL_HAS_AVX2_SOURCE != 0;
}

#if SMARTPARALLEL_HAS_AVX2_SOURCE
namespace
{
template <bool Inverse>
void threshold_avx2(const std::uint8_t* source,
                    std::uint8_t* destination,
                    std::size_t count,
                    std::uint8_t threshold,
                    std::uint8_t maximum) noexcept
{
    constexpr std::size_t width = 32;
    const __m256i sign = _mm256_set1_epi8(static_cast<char>(0x80));
    const __m256i threshold_value = _mm256_set1_epi8(
        static_cast<char>(threshold ^ std::uint8_t{0x80}));
    const __m256i maximum_value = _mm256_set1_epi8(static_cast<char>(maximum));
    std::size_t index = 0;
    for (; index + width <= count; index += width)
    {
        const __m256i values = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(source + index));
        const __m256i adjusted = _mm256_xor_si256(values, sign);
        const __m256i mask = _mm256_cmpgt_epi8(adjusted, threshold_value);
        const __m256i result = Inverse
            ? _mm256_andnot_si256(mask, maximum_value)
            : _mm256_and_si256(mask, maximum_value);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(destination + index), result);
    }
    if (index < count)
    {
        if constexpr (Inverse)
            threshold_sse2_inverse(source + index, destination + index,
                                   count - index, threshold, maximum);
        else
            threshold_sse2_binary(source + index, destination + index,
                                  count - index, threshold, maximum);
    }
}
} // namespace

void threshold_avx2_binary(const std::uint8_t* source,
                           std::uint8_t* destination,
                           std::size_t count,
                           std::uint8_t threshold,
                           std::uint8_t maximum) noexcept
{
    threshold_avx2<false>(source, destination, count, threshold, maximum);
}

void threshold_avx2_inverse(const std::uint8_t* source,
                            std::uint8_t* destination,
                            std::size_t count,
                            std::uint8_t threshold,
                            std::uint8_t maximum) noexcept
{
    threshold_avx2<true>(source, destination, count, threshold, maximum);
}
#else
void threshold_avx2_binary(const std::uint8_t* source,
                           std::uint8_t* destination,
                           std::size_t count,
                           std::uint8_t threshold,
                           std::uint8_t maximum) noexcept
{
    threshold_scalar_binary(source, destination, count, threshold, maximum);
}
void threshold_avx2_inverse(const std::uint8_t* source,
                            std::uint8_t* destination,
                            std::size_t count,
                            std::uint8_t threshold,
                            std::uint8_t maximum) noexcept
{
    threshold_scalar_inverse(source, destination, count, threshold, maximum);
}
#endif
} // namespace smart::vision::detail

#undef SMARTPARALLEL_HAS_AVX2_SOURCE
