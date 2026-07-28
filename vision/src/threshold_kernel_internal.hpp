#pragma once

#include <cstddef>
#include <cstdint>

namespace smart::vision::detail
{
std::uint64_t threshold_x86_xcr0() noexcept;

using ThresholdKernelFunction = void (*)(const std::uint8_t*,
                                         std::uint8_t*,
                                         std::size_t,
                                         std::uint8_t,
                                         std::uint8_t) noexcept;

void threshold_scalar_binary(const std::uint8_t* source,
                             std::uint8_t* destination,
                             std::size_t count,
                             std::uint8_t threshold,
                             std::uint8_t maximum) noexcept;
void threshold_scalar_inverse(const std::uint8_t* source,
                              std::uint8_t* destination,
                              std::size_t count,
                              std::uint8_t threshold,
                              std::uint8_t maximum) noexcept;

bool threshold_sse2_compiled() noexcept;
void threshold_sse2_binary(const std::uint8_t* source,
                           std::uint8_t* destination,
                           std::size_t count,
                           std::uint8_t threshold,
                           std::uint8_t maximum) noexcept;
void threshold_sse2_inverse(const std::uint8_t* source,
                            std::uint8_t* destination,
                            std::size_t count,
                            std::uint8_t threshold,
                            std::uint8_t maximum) noexcept;

bool threshold_avx2_compiled() noexcept;
void threshold_avx2_binary(const std::uint8_t* source,
                           std::uint8_t* destination,
                           std::size_t count,
                           std::uint8_t threshold,
                           std::uint8_t maximum) noexcept;
void threshold_avx2_inverse(const std::uint8_t* source,
                            std::uint8_t* destination,
                            std::size_t count,
                            std::uint8_t threshold,
                            std::uint8_t maximum) noexcept;
} // namespace smart::vision::detail
