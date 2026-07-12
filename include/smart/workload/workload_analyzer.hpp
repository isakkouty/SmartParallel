#pragma once

#include <cstddef>

#include <smart/workload/workload.hpp>
#include <smart/hardware/hardware_characteristics.hpp>

namespace smart
{
    struct WorkloadAnalysis
    {
        bool is_small = false;
        bool is_multidimensional = false;
        bool objects_are_large = false;

        bool has_many_iterations = false;
        bool is_memory_heavy = false;
        std::size_t iterations = 0;

        std::size_t working_set_bytes = 0;
    };

    // WorkloadAnalyzer extracts objective facts from the workload description.
    //
    // It intentionally does NOT attempt to infer computational characteristics
    // (such as arithmetic intensity or compute pressure), because those depend
    // on the user-provided function and cannot be determined from the workload
    // structure alone.
    //
    // Higher-level interpretation belongs to PerformanceModel.

    class WorkloadAnalyzer
    {
    public:
        WorkloadAnalysis analyze(const Workload& workload) const
        {
            WorkloadAnalysis analysis;
            HardwareCharacteristics hw = hardware_characteristics();

            analysis.iterations = workload.iterations;

            // Heuristic: "small" depends on work per iteration, which the analyzer
            // cannot know yet. This is only a temporary scheduling hint.
            analysis.is_small = workload.iterations < 1000;
 
            analysis.is_multidimensional = workload.dimensions.size() > 1;

            // Heuristic: many iterations does not mean CPU-heavy. It only tells us
            // the loop has enough iterations that parallel overhead may be worth considering.
            analysis.has_many_iterations = workload.iterations >= 1'000'000;

            std::size_t large_object_threshold = hw.cache_line_size * 2;
            if (large_object_threshold == 0)
            {
                // Fallback only if hardware cache-line detection failed.
                large_object_threshold = 128;
            }

            for (const Dimension& dimension : workload.dimensions)
            {
                analysis.working_set_bytes += dimension.size * dimension.object_size;

                if (dimension.object_size >= large_object_threshold)
                {
                    analysis.objects_are_large = true;
                }
            }

            std::size_t memory_heavy_threshold = hw.l3_cache_size;

            if (memory_heavy_threshold == 0)
            {
                // Fallback only if hardware L3 cache detection failed.
                memory_heavy_threshold = 32 * 1024 * 1024;
            }

            analysis.is_memory_heavy =
                analysis.working_set_bytes >= memory_heavy_threshold;

            return analysis;
        }
    };
}
