#pragma once

#include <smart/numerical/policy.hpp>

#include <stdexcept>

namespace smart::detail
{
struct NumericalCapabilities
{
    bool fast = true;
    bool reproducible = false;
    bool accurate = false;

    constexpr bool supports(NumericalPolicy policy) const noexcept
    {
        return policy == NumericalPolicy::Fast ? fast
             : policy == NumericalPolicy::Reproducible ? reproducible
             : accurate;
    }
};

inline constexpr NumericalCapabilities native_pointwise_capabilities{true, true, true};
inline constexpr NumericalCapabilities native_pairwise_reduction_capabilities{true, true, false};
inline constexpr NumericalCapabilities native_compensated_reduction_capabilities{true, true, true};
inline constexpr NumericalCapabilities native_scaled_sumsq_capabilities{true, true, true};

inline void require_numerical_capability(const NumericalCapabilities& capabilities,
                                         NumericalPolicy policy)
{
    if (!capabilities.supports(policy))
        throw std::invalid_argument(
            "SmartParallel numerical execution candidate does not support the requested policy");
}
} // namespace smart::detail
