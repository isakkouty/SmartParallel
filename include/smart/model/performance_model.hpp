#pragma once

#include <algorithm>
#include <cstddef>

#include <smart/workload/workload_analyzer.hpp>
#include <smart/hardware/hardware_characteristics.hpp>
#include <smart/model/performance_features.hpp>
#include <smart/decision/execution_hints.hpp>

namespace smart
{
    // PerformanceModel derives hardware-relative interpretations from known facts.
    //
    // It does not attempt to infer properties of the user-provided function
    // unless FunctionFeatures are explicitly available.
    //
    // The pressure values below are ratios:
    //   pressure = working_set_bytes / hardware_capacity
    //
    // Example:
    //   l3_pressure = 2.0 means the working set is twice the size of L3 cache.

    struct PerformanceModel
    {
        // Source feature groups
        WorkloadFeatures workload;
        HardwareCharacteristics hardware;
        FunctionFeatures function;

        // Derived memory hierarchy pressure
        double l1_pressure = 0.0;
        double l2_pressure = 0.0;
        double l3_pressure = 0.0;
        double page_pressure = 0.0;

        // Derived high-level interpretation
        double cache_pressure = 0.0;

        bool working_set_exceeds_l3 = false;
        bool likely_memory_sensitive = false;

        // Phase 1 canonical-observation integration. These values document
        // whether the model consumed the sensor layer directly or had to
        // derive compatible fallbacks from hardware capacities.
        bool used_structural_cache_observations = false;
        ObservationMetadata structural_observation_metadata{};
    };

    class PerformanceModelBuilder
    {
    public:

        
        PerformanceModel build(const WorkloadAnalysis& analysis) const
        {
            ExecutionHints hints;
            return build(analysis, hints);
        }
            
        PerformanceModel build( const WorkloadAnalysis& analysis, const ExecutionHints& hints) const
        {
            PerformanceModel model;
            HardwareCharacteristics hw = hardware_characteristics();

            model.workload.iterations =
                analysis.structural.logical_iterations;
            const std::size_t represented =
                analysis.structural.represented_input_bytes;
            const std::size_t external = hints.available
                ? hints.external_working_set_bytes
                : 0;
            model.workload.working_set_bytes =
                external > static_cast<std::size_t>(-1) - represented
                    ? static_cast<std::size_t>(-1)
                    : represented + external;
            model.workload.is_multidimensional = analysis.is_multidimensional;
            model.workload.has_large_objects = analysis.objects_are_large;
            model.workload.has_few_iterations = analysis.is_small;
            model.workload.has_many_iterations = analysis.has_many_iterations;

            model.hardware = hw;
            model.structural_observation_metadata =
                analysis.structural.metadata;

            model.function.available = hints.available;
            model.function.arithmetic_intensity = hints.arithmetic_intensity;
            model.function.branchiness = hints.branchiness;
            model.function.memory_randomness = hints.memory_randomness;
            model.function.vectorization_potential = hints.vectorization_potential;
            model.function.dependency_depth = hints.dependency_depth;
            model.function.dependent_memory_accesses_per_iteration =
                hints.dependent_memory_accesses_per_iteration;
            model.function.external_working_set_bytes =
                hints.external_working_set_bytes;
            model.function.bytes_touched_per_iteration =
                hints.bytes_touched_per_iteration;
            model.function.estimated_memory_level_parallelism =
                hints.estimated_memory_level_parallelism;
            model.function.feature_confidence = hints.feature_confidence;

            std::size_t l1_cache_size = model.hardware.l1_cache_size;
            std::size_t l2_cache_size = model.hardware.l2_cache_size;
            std::size_t l3_cache_size = model.hardware.l3_cache_size;
            std::size_t page_size = model.hardware.page_size;

            if (l1_cache_size == 0)
            {
                // Fallback only if hardware L1 detection failed.
                l1_cache_size = 32 * 1024;
            }

            if (l2_cache_size == 0)
            {
                // Fallback only if hardware L2 detection failed.
                l2_cache_size = 512 * 1024;
            }

            if (l3_cache_size == 0)
            {
                // Fallback only if hardware L3 detection failed.
                l3_cache_size = 32 * 1024 * 1024;
            }

            if (page_size == 0)
            {
                // Fallback only if hardware page-size detection failed.
                page_size = 4096;
            }

            if (analysis.structural.cache_ratios_available)
            {
                model.l1_pressure =
                    analysis.structural.l1_residency_ratio;
                model.l2_pressure =
                    analysis.structural.l2_residency_ratio;
                model.l3_pressure =
                    analysis.structural.l3_residency_ratio;
                model.used_structural_cache_observations = true;

                // Structural ratios describe the represented container. An
                // explicitly declared external working set (for example a
                // pointer chain captured by the callback) must extend those
                // ratios rather than being hidden by the structural source.
                if (external > 0)
                {
                    model.l1_pressure = std::max(
                        model.l1_pressure,
                        static_cast<double>(
                            model.workload.working_set_bytes) /
                            static_cast<double>(l1_cache_size));
                    model.l2_pressure = std::max(
                        model.l2_pressure,
                        static_cast<double>(
                            model.workload.working_set_bytes) /
                            static_cast<double>(l2_cache_size));
                    model.l3_pressure = std::max(
                        model.l3_pressure,
                        static_cast<double>(
                            model.workload.working_set_bytes) /
                            static_cast<double>(l3_cache_size));
                }
            }
            else
            {
                model.l1_pressure =
                    static_cast<double>(model.workload.working_set_bytes) /
                    static_cast<double>(l1_cache_size);

                model.l2_pressure =
                    static_cast<double>(model.workload.working_set_bytes) /
                    static_cast<double>(l2_cache_size);

                model.l3_pressure =
                    static_cast<double>(model.workload.working_set_bytes) /
                    static_cast<double>(l3_cache_size);
            }

            model.page_pressure =
                static_cast<double>(model.workload.working_set_bytes) /
                static_cast<double>(page_size);

            model.cache_pressure =
                model.l3_pressure >= 1.0 ? 1.0 : model.l3_pressure;

            model.working_set_exceeds_l3 =
                model.l3_pressure >= 1.0;

            model.likely_memory_sensitive =
                model.working_set_exceeds_l3 ||
                model.workload.has_large_objects;

            return model;
        }

    };
}
