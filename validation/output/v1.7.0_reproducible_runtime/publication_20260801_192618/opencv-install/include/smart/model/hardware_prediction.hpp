#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <smart/hardware/hardware_characteristics.hpp>
#include <smart/workload/workload_analyzer.hpp>

namespace smart
{
struct HardwarePredictionContext
{
    double smt_ratio = 1.0;
    double working_set_per_worker_bytes = 0.0;
    double l2_pressure_per_worker = 0.0;
    double l3_pressure = 0.0;
    double page_pressure = 0.0;
    double numa_pressure = 0.0;

    double locality_score = 0.5;

    double bandwidth_parallelism_limit = 1.0;
    bool cache_information_available = false;
};

inline HardwarePredictionContext hardware_prediction_context(
    const WorkloadAnalysis& analysis, const HardwareCharacteristics& hardware, std::size_t workers)
{
    HardwarePredictionContext context;
    workers = std::max<std::size_t>(1, workers);

    const std::size_t physical = std::max<std::size_t>(
        1, hardware.physical_cores == 0 ? hardware.logical_threads : hardware.physical_cores);
    const std::size_t logical = std::max<std::size_t>(1, hardware.logical_threads);
    context.smt_ratio = std::max(1.0, static_cast<double>(logical) / static_cast<double>(physical));

    context.working_set_per_worker_bytes =
        static_cast<double>(analysis.working_set_bytes) / static_cast<double>(workers);

    const double l2 =
        static_cast<double>(hardware.l2_cache_size > 0 ? hardware.l2_cache_size : 512 * 1024);
    const double l3 =
        static_cast<double>(hardware.l3_cache_size > 0 ? hardware.l3_cache_size : 32 * 1024 * 1024);
    const double page = static_cast<double>(hardware.page_size > 0 ? hardware.page_size : 4096);

    context.l2_pressure_per_worker = context.working_set_per_worker_bytes / std::max(1.0, l2);
    context.l3_pressure = static_cast<double>(analysis.working_set_bytes) / std::max(1.0, l3);
    context.page_pressure = static_cast<double>(analysis.working_set_bytes) / std::max(1.0, page);
    context.cache_information_available = hardware.cache_info_available;

    double locality_sum = 0.0;
    double locality_weight = 0.0;
    for (const DimensionAnalysis& dimension : analysis.structural.dimensions)
    {
        double locality = 0.50;
        if (dimension.contiguous_known)
            locality += dimension.contiguous ? 0.30 : -0.20;
        if (dimension.random_access_known)
            locality += dimension.random_access ? -0.30 : 0.15;
        if (dimension.stride_known && hardware.cache_line_size > 0)
        {
            const double stride_lines = static_cast<double>(dimension.stride_bytes)
                                        / static_cast<double>(hardware.cache_line_size);
            locality -= std::min(0.25, std::max(0.0, stride_lines - 1.0) * 0.04);
        }
        if (dimension.storage_kind == StorageKind::NodeBased
            || dimension.storage_kind == StorageKind::Segmented)
            locality -= 0.20;
        locality_sum += std::clamp(locality, 0.0, 1.0);
        locality_weight += 1.0;
    }
    if (locality_weight > 0.0)
        context.locality_score = locality_sum / locality_weight;

    const std::size_t numa_nodes = std::max<std::size_t>(1, hardware.numa_nodes);
    if (numa_nodes > 1 && context.l3_pressure > 1.0)
    {
        context.numa_pressure = std::clamp(
            std::log2(context.l3_pressure + 1.0) * (1.0 - context.locality_score)
                * (static_cast<double>(numa_nodes - 1) / static_cast<double>(numa_nodes)),
            0.0,
            1.0);
    }

    const double locality_limit = 0.35 + 0.65 * context.locality_score;
    const double cache_limit = 1.0 / (1.0 + 0.12 * std::log2(std::max(1.0, context.l3_pressure)));
    const double smt_limit =
        workers > physical ? 1.0 / (1.0 + 0.18 * (context.smt_ratio - 1.0)) : 1.0;
    const double numa_limit = 1.0 - 0.35 * context.numa_pressure;

    context.bandwidth_parallelism_limit =
        std::clamp(locality_limit * cache_limit * smt_limit * numa_limit, 0.20, 1.0);
    return context;
}
} // namespace smart
