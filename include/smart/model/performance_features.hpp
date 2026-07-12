#pragma once

#include <cstddef>

namespace smart
{
    struct WorkloadFeatures
    {
        std::size_t iterations = 0;
        std::size_t working_set_bytes = 0;

        bool is_multidimensional = false;
        bool has_large_objects = false;
        bool has_few_iterations = false;
        bool has_many_iterations = false;
    };

    struct FunctionFeatures
    {
        bool available = false;

        double arithmetic_intensity = 0.0;
        double branchiness = 0.0;
        double memory_randomness = 0.0;
        double vectorization_potential = 0.0;
    };
}
