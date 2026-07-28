#include <smart/vision/detail/threshold_kernel.hpp>

#include "threshold_kernel_internal.hpp"

#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#elif (defined(__GNUC__) || defined(__clang__)) \
    && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
#endif

namespace smart::vision::detail
{
namespace
{
#if defined(_MSC_VER)
#define SMARTPARALLEL_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define SMARTPARALLEL_RESTRICT __restrict__
#else
#define SMARTPARALLEL_RESTRICT
#endif

void scalar_binary_disjoint(
    const std::uint8_t* SMARTPARALLEL_RESTRICT source,
    std::uint8_t* SMARTPARALLEL_RESTRICT destination,
    std::size_t count,
    std::uint8_t threshold,
    std::uint8_t maximum) noexcept
{
    const unsigned maximum_unsigned = maximum;
    for (std::size_t index = 0; index < count; ++index)
    {
        destination[index] = static_cast<std::uint8_t>(
            static_cast<unsigned>(source[index] > threshold) * maximum_unsigned);
    }
}

void scalar_inverse_disjoint(
    const std::uint8_t* SMARTPARALLEL_RESTRICT source,
    std::uint8_t* SMARTPARALLEL_RESTRICT destination,
    std::size_t count,
    std::uint8_t threshold,
    std::uint8_t maximum) noexcept
{
    const unsigned maximum_unsigned = maximum;
    for (std::size_t index = 0; index < count; ++index)
    {
        destination[index] = static_cast<std::uint8_t>(
            static_cast<unsigned>(source[index] <= threshold) * maximum_unsigned);
    }
}

void scalar_binary_in_place(const std::uint8_t* source,
                            std::uint8_t* destination,
                            std::size_t count,
                            std::uint8_t threshold,
                            std::uint8_t maximum) noexcept
{
    (void)source;
    const unsigned maximum_unsigned = maximum;
    for (std::size_t index = 0; index < count; ++index)
    {
        destination[index] = static_cast<std::uint8_t>(
            static_cast<unsigned>(destination[index] > threshold) * maximum_unsigned);
    }
}

void scalar_inverse_in_place(const std::uint8_t* source,
                             std::uint8_t* destination,
                             std::size_t count,
                             std::uint8_t threshold,
                             std::uint8_t maximum) noexcept
{
    (void)source;
    const unsigned maximum_unsigned = maximum;
    for (std::size_t index = 0; index < count; ++index)
    {
        destination[index] = static_cast<std::uint8_t>(
            static_cast<unsigned>(destination[index] <= threshold) * maximum_unsigned);
    }
}

bool cpu_supports_sse2() noexcept
{
#if defined(_M_X64) || defined(__x86_64__)
    return true;
#elif defined(_MSC_VER) && defined(_M_IX86)
    int registers[4]{};
    __cpuid(registers, 0);
    if (registers[0] < 1)
        return false;
    __cpuid(registers, 1);
    return (registers[3] & (1 << 26)) != 0;
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__i386__)
    __builtin_cpu_init();
    return __builtin_cpu_supports("sse2");
#else
    return false;
#endif
}

bool cpu_supports_avx2() noexcept
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int registers[4]{};
    __cpuid(registers, 0);
    if (registers[0] < 7)
        return false;
    __cpuid(registers, 1);
    const bool osxsave = (registers[2] & (1 << 27)) != 0;
    const bool avx = (registers[2] & (1 << 28)) != 0;
    if (!osxsave || !avx || (threshold_x86_xcr0() & 0x6u) != 0x6u)
        return false;
    __cpuidex(registers, 7, 0);
    return (registers[1] & (1 << 5)) != 0;
#elif (defined(__GNUC__) || defined(__clang__)) \
    && (defined(__x86_64__) || defined(__i386__))
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2");
#else
    return false;
#endif
}

struct KernelDispatch
{
    ThresholdKernelKind kind = ThresholdKernelKind::Scalar;
    ThresholdKernelFunction binary_disjoint = scalar_binary_disjoint;
    ThresholdKernelFunction inverse_disjoint = scalar_inverse_disjoint;
    ThresholdKernelFunction binary_in_place = scalar_binary_in_place;
    ThresholdKernelFunction inverse_in_place = scalar_inverse_in_place;
};

KernelDispatch detect_dispatch() noexcept
{
    if (threshold_avx2_compiled() && cpu_supports_avx2())
    {
        return {ThresholdKernelKind::AVX2,
                threshold_avx2_binary,
                threshold_avx2_inverse,
                threshold_avx2_binary,
                threshold_avx2_inverse};
    }
    if (threshold_sse2_compiled() && cpu_supports_sse2())
    {
        return {ThresholdKernelKind::SSE2,
                threshold_sse2_binary,
                threshold_sse2_inverse,
                threshold_sse2_binary,
                threshold_sse2_inverse};
    }
    return {};
}

const KernelDispatch& kernel_dispatch() noexcept
{
    static const KernelDispatch dispatch = detect_dispatch();
    return dispatch;
}

#undef SMARTPARALLEL_RESTRICT
} // namespace

void threshold_scalar_binary(const std::uint8_t* source,
                             std::uint8_t* destination,
                             std::size_t count,
                             std::uint8_t threshold,
                             std::uint8_t maximum) noexcept
{
    if (source == destination)
        scalar_binary_in_place(source, destination, count, threshold, maximum);
    else
        scalar_binary_disjoint(source, destination, count, threshold, maximum);
}

void threshold_scalar_inverse(const std::uint8_t* source,
                              std::uint8_t* destination,
                              std::size_t count,
                              std::uint8_t threshold,
                              std::uint8_t maximum) noexcept
{
    if (source == destination)
        scalar_inverse_in_place(source, destination, count, threshold, maximum);
    else
        scalar_inverse_disjoint(source, destination, count, threshold, maximum);
}

ThresholdKernelKind selected_threshold_kernel() noexcept
{
    return kernel_dispatch().kind;
}

const char* threshold_kernel_name(ThresholdKernelKind kind) noexcept
{
    switch (kind)
    {
        case ThresholdKernelKind::Scalar:
            return "scalar_branchless";
        case ThresholdKernelKind::SSE2:
            return "sse2";
        case ThresholdKernelKind::AVX2:
            return "avx2";
    }
    return "unknown";
}

bool threshold_kernel_uses_explicit_simd() noexcept
{
    return selected_threshold_kernel() != ThresholdKernelKind::Scalar;
}

void threshold_u8_contiguous_disjoint_binary(const std::uint8_t* source,
                                              std::uint8_t* destination,
                                              std::size_t count,
                                              std::uint8_t threshold,
                                              std::uint8_t maximum) noexcept
{
    kernel_dispatch().binary_disjoint(source, destination, count, threshold, maximum);
}

void threshold_u8_contiguous_disjoint_inverse(const std::uint8_t* source,
                                               std::uint8_t* destination,
                                               std::size_t count,
                                               std::uint8_t threshold,
                                               std::uint8_t maximum) noexcept
{
    kernel_dispatch().inverse_disjoint(source, destination, count, threshold, maximum);
}

void threshold_u8_contiguous_in_place_binary(std::uint8_t* data,
                                             std::size_t count,
                                             std::uint8_t threshold,
                                             std::uint8_t maximum) noexcept
{
    kernel_dispatch().binary_in_place(data, data, count, threshold, maximum);
}

void threshold_u8_contiguous_in_place_inverse(std::uint8_t* data,
                                              std::size_t count,
                                              std::uint8_t threshold,
                                              std::uint8_t maximum) noexcept
{
    kernel_dispatch().inverse_in_place(data, data, count, threshold, maximum);
}
} // namespace smart::vision::detail
