#pragma once

#include <cstddef>

namespace smart
{
struct ExecutionHints
{
    bool available = false;

    // Values are normalized to [0, 1] unless documented otherwise.
    double arithmetic_intensity = 0.0;
    double branchiness = 0.0;
    double memory_randomness = 0.0;
    double vectorization_potential = 0.0;

    // Phase 11 memory-observability hints. These are optional and remain
    // neutral when unset. They describe the callback's actual access
    // behavior, not merely the storage capabilities of its container.
    double dependency_depth = 0.0;
    double dependent_memory_accesses_per_iteration = 0.0;
    std::size_t external_working_set_bytes = 0;
    double bytes_touched_per_iteration = 0.0;
    double estimated_memory_level_parallelism = 0.0;

    // Confidence in the hints themselves. A caller that supplies hints but
    // cannot guarantee their quality may lower this value.
    double feature_confidence = 1.0;
};

inline ExecutionHints compute_heavy()
{
    ExecutionHints hints;
    hints.available = true;
    hints.arithmetic_intensity = 1.0;
    return hints;
}

inline ExecutionHints memory_random()
{
    ExecutionHints hints;
    hints.available = true;
    hints.memory_randomness = 1.0;
    hints.dependent_memory_accesses_per_iteration = 1.0;
    hints.dependency_depth = 1.0;
    hints.estimated_memory_level_parallelism = 1.0;
    return hints;
}

inline ExecutionHints pointer_chasing(std::size_t external_working_set_bytes = 0,
                                      double dependent_accesses_per_iteration = 1.0)
{
    ExecutionHints hints = memory_random();
    hints.external_working_set_bytes = external_working_set_bytes;
    hints.dependent_memory_accesses_per_iteration = dependent_accesses_per_iteration;
    hints.dependency_depth = dependent_accesses_per_iteration;
    hints.estimated_memory_level_parallelism = 1.0;
    return hints;
}
} // namespace smart
