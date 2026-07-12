#pragma once

namespace smart
{
    struct ExecutionHints
    {
        bool available = false;

        double arithmetic_intensity = 0.0;
        double branchiness = 0.0;
        double memory_randomness = 0.0;
        double vectorization_potential = 0.0;
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
        return hints;
    }
}
