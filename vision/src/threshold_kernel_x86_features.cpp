#include "threshold_kernel_internal.hpp"

#include <cstdint>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace smart::vision::detail
{
std::uint64_t threshold_x86_xcr0() noexcept
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    return static_cast<std::uint64_t>(_xgetbv(0));
#else
    return 0;
#endif
}
} // namespace smart::vision::detail
