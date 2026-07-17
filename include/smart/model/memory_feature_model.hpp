#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <smart/decision/execution_hints.hpp>
#include <smart/model/performance_model.hpp>
#include <smart/workload/workload_analyzer.hpp>

namespace smart
{
    enum class MemoryAccessPattern
    {
        Unknown,
        Sequential,
        Strided,
        Random,
        PointerChasing,
        Mixed
    };

    enum class WorkingSetTier
    {
        Unknown,
        L1Resident,
        L2Resident,
        L3Resident,
        MemoryResident
    };

    struct MemoryFeatures
    {
        MemoryAccessPattern access_pattern = MemoryAccessPattern::Unknown;
        WorkingSetTier working_set_tier = WorkingSetTier::Unknown;

        double confidence = 0.0;
        double bytes_touched_per_iteration = 0.0;
        double arithmetic_intensity = 0.0;
        double random_access_ratio = 0.0;
        double contiguous_ratio = 0.0;
        double cache_reuse_score = 0.0;
        double memory_level_parallelism = 0.0;
        double dependency_score = 0.0;
        double l3_pressure = 0.0;

        bool bandwidth_sensitive = false;
        bool latency_sensitive = false;
        bool cache_resident = false;
        bool large_record = false;
    };

    // Phase 13: one canonical, confidence-scored description of memory
    // behaviour. Structural observations and explicit callback hints are kept
    // separate and blended only when both are available. Unknown information
    // remains neutral rather than being inferred from the selected family.
    class MemoryFeatureModelBuilder
    {
    public:
        MemoryFeatures build(
            const WorkloadAnalysis& analysis,
            const PerformanceModel& model,
            const ExecutionHints& hints) const
        {
            MemoryFeatures result;
            if (analysis.structural.logical_iterations == 0)
                return result;

            std::size_t contiguous_known = 0;
            std::size_t contiguous_count = 0;
            std::size_t random_known = 0;
            std::size_t random_count = 0;
            std::size_t strided_count = 0;
            bool node_or_segmented = false;

            for (const DimensionAnalysis& dimension : analysis.structural.dimensions)
            {
                if (dimension.contiguous_known)
                {
                    ++contiguous_known;
                    contiguous_count += dimension.contiguous ? 1u : 0u;
                }
                if (dimension.random_access_known)
                {
                    ++random_known;
                    random_count += dimension.random_access ? 1u : 0u;
                }
                if (dimension.stride_known && dimension.stride_bytes > 64)
                {
                    ++strided_count;
                }
                node_or_segmented = node_or_segmented ||
                    dimension.storage_kind == StorageKind::NodeBased ||
                    dimension.storage_kind == StorageKind::Segmented;
            }

            result.contiguous_ratio = contiguous_known == 0
                ? 0.0
                : static_cast<double>(contiguous_count) /
                    static_cast<double>(contiguous_known);
            const double structural_random = random_known == 0
                ? 0.0
                : static_cast<double>(random_count) /
                    static_cast<double>(random_known);

            const double hint_confidence = hints.available
                ? std::clamp(hints.feature_confidence, 0.0, 1.0)
                : 0.0;
            result.random_access_ratio = hints.available
                ? (1.0 - hint_confidence) * structural_random +
                    hint_confidence * std::clamp(hints.memory_randomness, 0.0, 1.0)
                : structural_random;
            result.arithmetic_intensity = hints.available
                ? std::clamp(hints.arithmetic_intensity, 0.0, 1.0)
                : 0.0;
            result.memory_level_parallelism = hints.available
                ? std::clamp(hints.estimated_memory_level_parallelism / 8.0, 0.0, 1.0)
                : 0.0;
            result.dependency_score = hints.available
                ? std::clamp(
                    std::max(hints.dependency_depth,
                        hints.dependent_memory_accesses_per_iteration) / 8.0,
                    0.0,
                    1.0)
                : 0.0;

            const double represented_per_iteration =
                static_cast<double>(analysis.structural.represented_input_bytes) /
                static_cast<double>(analysis.structural.logical_iterations);
            result.bytes_touched_per_iteration = hints.available &&
                    hints.bytes_touched_per_iteration > 0.0
                ? hints.bytes_touched_per_iteration
                : represented_per_iteration;
            result.l3_pressure = std::max(
                model.l3_pressure,
                analysis.structural.cache_ratios_available
                    ? analysis.structural.l3_residency_ratio
                    : 0.0);

            if (model.l1_pressure > 0.0 && model.l1_pressure <= 0.85)
                result.working_set_tier = WorkingSetTier::L1Resident;
            else if (model.l2_pressure > 0.0 && model.l2_pressure <= 0.85)
                result.working_set_tier = WorkingSetTier::L2Resident;
            else if (result.l3_pressure > 0.0 && result.l3_pressure <= 0.85)
                result.working_set_tier = WorkingSetTier::L3Resident;
            else if (result.l3_pressure > 0.85)
                result.working_set_tier = WorkingSetTier::MemoryResident;

            const bool pointer_chasing = hints.available &&
                (hints.dependency_depth > 0.0 ||
                 hints.dependent_memory_accesses_per_iteration > 0.0) &&
                result.random_access_ratio >= 0.45;
            const bool random_access = result.random_access_ratio >= 0.55 ||
                node_or_segmented;
            const bool sequential = result.contiguous_ratio >= 0.70 &&
                result.random_access_ratio < 0.30;
            const bool strided = strided_count > 0 && !random_access;

            if (pointer_chasing)
                result.access_pattern = MemoryAccessPattern::PointerChasing;
            else if (random_access && sequential)
                result.access_pattern = MemoryAccessPattern::Mixed;
            else if (random_access)
                result.access_pattern = MemoryAccessPattern::Random;
            else if (strided)
                result.access_pattern = MemoryAccessPattern::Strided;
            else if (sequential)
                result.access_pattern = MemoryAccessPattern::Sequential;

            result.cache_resident =
                result.working_set_tier == WorkingSetTier::L1Resident ||
                result.working_set_tier == WorkingSetTier::L2Resident ||
                result.working_set_tier == WorkingSetTier::L3Resident;
            result.large_record = analysis.objects_are_large ||
                result.bytes_touched_per_iteration >= 128.0;
            result.latency_sensitive =
                result.access_pattern == MemoryAccessPattern::Random ||
                result.access_pattern == MemoryAccessPattern::PointerChasing ||
                result.dependency_score >= 0.25;
            result.bandwidth_sensitive =
                result.access_pattern == MemoryAccessPattern::Sequential &&
                result.working_set_tier == WorkingSetTier::MemoryResident &&
                result.arithmetic_intensity <= 0.45;

            const double locality = std::clamp(
                0.65 * result.contiguous_ratio +
                0.35 * (1.0 - result.random_access_ratio),
                0.0,
                1.0);
            const double residence = result.cache_resident ? 1.0 :
                std::clamp(1.0 / std::max(1.0, result.l3_pressure), 0.0, 1.0);
            result.cache_reuse_score = locality * residence;

            double evidence = 0.10;
            if (contiguous_known > 0) evidence += 0.18;
            if (random_known > 0) evidence += 0.18;
            if (analysis.structural.cache_ratios_available) evidence += 0.18;
            if (analysis.structural.represented_input_bytes > 0) evidence += 0.10;
            if (strided_count > 0 || node_or_segmented) evidence += 0.08;
            if (hints.available) evidence += 0.18 * hint_confidence;
            result.confidence = std::clamp(evidence, 0.0, 1.0);
            return result;
        }
    };
}
