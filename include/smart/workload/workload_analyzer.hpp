#pragma once

#include <cstddef>
#include <smart/core/config.hpp>
#include <smart/core/safe_arithmetic.hpp>
#include <smart/hardware/hardware_characteristics.hpp>
#include <smart/workload/observation.hpp>
#include <smart/workload/workload.hpp>
#include <vector>

namespace smart
{
struct DimensionAnalysis
{
    std::size_t extent = 0;
    std::size_t representation_bytes = 0;
    std::size_t unique_elements = 0;
    std::size_t reuse_factor = 1;

    StorageKind storage_kind = StorageKind::Unknown;
    bool contiguous = false;

    bool contiguous_known = false;
    bool random_access = false;
    bool random_access_known = false;
    std::size_t stride_bytes = 0;
    bool stride_known = false;

    double elements_per_cache_line = 0.0;

    double cache_lines_per_element = 0.0;
    bool spans_multiple_cache_lines = false;
};

struct StructuralObservations
{
    ObservationMetadata metadata{ObservationSource::Derived, ObservationConfidence::High};

    std::size_t logical_iterations = 0;
    std::size_t dimensionality = 0;
    std::size_t represented_input_bytes = 0;
    bool represented_input_bytes_saturated = false;
    std::size_t unique_input_elements = 0;
    bool unique_input_elements_saturated = false;

    double iterations_per_logical_thread = 0.0;
    double iterations_per_physical_core = 0.0;

    double l1_residency_ratio = 0.0;
    double l2_residency_ratio = 0.0;
    double l3_residency_ratio = 0.0;
    bool cache_ratios_available = false;

    std::vector<DimensionAnalysis> dimensions;
};

struct WorkloadAnalysis
{
    StructuralObservations structural;

    bool is_small = false;
    bool is_multidimensional = false;
    bool objects_are_large = false;
    bool has_many_iterations = false;

    bool is_memory_heavy = false;

    std::size_t iterations = 0;

    // Compatibility alias for represented input bytes. It is not a
    // measurement of the bytes actually touched by the callback.
    std::size_t working_set_bytes = 0;
    bool working_set_saturated = false;
    bool iteration_count_saturated = false;
};

class WorkloadAnalyzer
{
  public:
    WorkloadAnalysis analyze(const Workload& workload) const
    {
        WorkloadAnalysis analysis;
        const HardwareCharacteristics hardware = hardware_characteristics();

        analysis.iterations = workload.iterations;
        analysis.iteration_count_saturated = workload.iterations_saturated;

        analysis.structural.logical_iterations = workload.iterations;
        analysis.structural.dimensionality = workload.dimensions.size();

        analysis.is_small =
            workload.iterations < global_config().small_workload_iteration_threshold;
        analysis.is_multidimensional = workload.dimensions.size() > 1;
        analysis.has_many_iterations =
            workload.iterations >= global_config().many_iterations_threshold;

        const std::size_t logical_threads =
            hardware.logical_threads == 0 ? 1 : hardware.logical_threads;
        const std::size_t physical_cores =
            hardware.physical_cores == 0 ? logical_threads : hardware.physical_cores;

        analysis.structural.iterations_per_logical_thread =
            static_cast<double>(workload.iterations) / static_cast<double>(logical_threads);

        analysis.structural.iterations_per_physical_core =
            static_cast<double>(workload.iterations) / static_cast<double>(physical_cores);

        std::size_t large_object_threshold = hardware.cache_line_size * 2;
        if (large_object_threshold == 0)
        {
            large_object_threshold = 128;
        }

        for (std::size_t index = 0; index < workload.dimensions.size(); ++index)
        {
            const Dimension& dimension = workload.dimensions[index];
            DimensionAnalysis dimension_analysis;
            dimension_analysis.extent = dimension.size;
            dimension_analysis.representation_bytes = dimension.object_size;
            dimension_analysis.unique_elements = dimension.size;
            dimension_analysis.storage_kind = dimension.storage_kind;
            dimension_analysis.contiguous = dimension.contiguous;
            dimension_analysis.contiguous_known = dimension.contiguous_known;
            dimension_analysis.random_access = dimension.random_access;
            dimension_analysis.random_access_known = dimension.random_access_known;
            dimension_analysis.stride_bytes = dimension.stride_bytes;
            dimension_analysis.stride_known = dimension.stride_known;

            if (analysis.is_multidimensional && dimension.size > 0)
            {
                dimension_analysis.reuse_factor = workload.iterations / dimension.size;
            }

            const SizeCalculation dimension_bytes =
                saturating_multiply(dimension.size, dimension.object_size);

            const SizeCalculation total_bytes =
                saturating_add(analysis.structural.represented_input_bytes, dimension_bytes.value);

            analysis.structural.represented_input_bytes = total_bytes.value;
            analysis.structural.represented_input_bytes_saturated =
                analysis.structural.represented_input_bytes_saturated || dimension_bytes.saturated
                || total_bytes.saturated;

            const SizeCalculation total_unique =
                saturating_add(analysis.structural.unique_input_elements, dimension.size);

            analysis.structural.unique_input_elements = total_unique.value;
            analysis.structural.unique_input_elements_saturated =
                analysis.structural.unique_input_elements_saturated || total_unique.saturated;

            if (dimension.object_size >= large_object_threshold)
            {
                analysis.objects_are_large = true;
            }

            if (hardware.cache_line_size > 0 && dimension.object_size > 0)
            {
                dimension_analysis.elements_per_cache_line =
                    static_cast<double>(hardware.cache_line_size)
                    / static_cast<double>(dimension.object_size);

                dimension_analysis.cache_lines_per_element =
                    static_cast<double>(dimension.object_size)
                    / static_cast<double>(hardware.cache_line_size);

                dimension_analysis.spans_multiple_cache_lines =
                    dimension.object_size > hardware.cache_line_size;
            }

            analysis.structural.dimensions.push_back(dimension_analysis);
        }

        analysis.working_set_bytes = analysis.structural.represented_input_bytes;
        analysis.working_set_saturated = analysis.structural.represented_input_bytes_saturated;

        if (hardware.l1_cache_size > 0 && hardware.l2_cache_size > 0 && hardware.l3_cache_size > 0)
        {
            const double bytes = static_cast<double>(analysis.structural.represented_input_bytes);

            analysis.structural.l1_residency_ratio =
                bytes / static_cast<double>(hardware.l1_cache_size);
            analysis.structural.l2_residency_ratio =
                bytes / static_cast<double>(hardware.l2_cache_size);
            analysis.structural.l3_residency_ratio =
                bytes / static_cast<double>(hardware.l3_cache_size);
            analysis.structural.cache_ratios_available = true;
        }

        std::size_t memory_heavy_threshold = hardware.l3_cache_size;
        if (memory_heavy_threshold == 0)
        {
            memory_heavy_threshold = 32 * 1024 * 1024;
        }

        analysis.is_memory_heavy =
            analysis.working_set_saturated || analysis.working_set_bytes >= memory_heavy_threshold;

        return analysis;
    }
};
} // namespace smart
