#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

#include <smart/decision/execution_hints.hpp>
#include <smart/model/performance_model.hpp>
#include <smart/profiling/function_profiler.hpp>
#include <smart/workload/workload_analyzer.hpp>

namespace smart
{
    enum class WorkloadFamily
    {
        Unknown,
        ComputeHeavy,
        StreamingMemory,
        IrregularMemory,
        BranchHeavy,
        Mixed
    };

    inline constexpr std::string_view to_string(WorkloadFamily family)
    {
        switch (family)
        {
        case WorkloadFamily::ComputeHeavy:
            return "ComputeHeavy";
        case WorkloadFamily::StreamingMemory:
            return "StreamingMemory";
        case WorkloadFamily::IrregularMemory:
            return "IrregularMemory";
        case WorkloadFamily::BranchHeavy:
            return "BranchHeavy";
        case WorkloadFamily::Mixed:
            return "Mixed";
        case WorkloadFamily::Unknown:
        default:
            return "Unknown";
        }
    }

    struct WorkloadFamilyEvidence
    {
        double compute = 0.0;
        double streaming_memory = 0.0;
        double irregular_memory = 0.0;
        double branch = 0.0;
        double mixed = 0.0;
    };

    struct WorkloadFamilyClassification
    {
        WorkloadFamily family = WorkloadFamily::Unknown;
        double confidence = 0.0;
        WorkloadFamilyEvidence evidence{};

        bool used_execution_hints = false;
        bool used_structural_observations = false;
        bool used_profile_observations = false;
        bool ambiguous = true;
    };

    class WorkloadFamilyClassifier
    {
    public:
        WorkloadFamilyClassification classify(
            const WorkloadAnalysis& analysis,
            const PerformanceModel& model,
            const FunctionProfile* profile = nullptr,
            const ExecutionHints* hints = nullptr) const
        {
            WorkloadFamilyClassification result;
            WorkloadFamilyEvidence& score = result.evidence;

            const bool hints_available =
                hints != nullptr && hints->available;
            result.used_execution_hints = hints_available;

            if (hints_available)
            {
                const double arithmetic = clamp01(hints->arithmetic_intensity);
                const double branchiness = clamp01(hints->branchiness);
                const double randomness = clamp01(hints->memory_randomness);
                const double vectorization = clamp01(hints->vectorization_potential);

                score.compute += 2.4 * arithmetic + 0.5 * vectorization;
                score.branch += 2.5 * branchiness;
                score.irregular_memory += 2.6 * randomness;
                score.streaming_memory +=
                    0.8 * vectorization * (1.0 - randomness);

                if ((arithmetic >= 0.35 && randomness >= 0.35) ||
                    (branchiness >= 0.35 && arithmetic >= 0.35) ||
                    (branchiness >= 0.35 && randomness >= 0.35))
                {
                    score.mixed += 1.8;
                }
            }

            result.used_structural_observations = true;

            std::size_t known_contiguous = 0;
            std::size_t contiguous = 0;
            std::size_t known_random = 0;
            std::size_t random = 0;
            std::size_t known_stride = 0;
            std::size_t regular_stride = 0;

            for (const DimensionAnalysis& dimension : analysis.structural.dimensions)
            {
                if (dimension.contiguous_known)
                {
                    ++known_contiguous;
                    contiguous += dimension.contiguous ? 1u : 0u;
                }
                if (dimension.random_access_known)
                {
                    ++known_random;
                    random += dimension.random_access ? 1u : 0u;
                }
                if (dimension.stride_known)
                {
                    ++known_stride;
                    regular_stride += dimension.stride_bytes > 0 ? 1u : 0u;
                }

                if (dimension.storage_kind == StorageKind::NodeBased ||
                    dimension.storage_kind == StorageKind::Segmented)
                {
                    score.irregular_memory += 0.9;
                }
                if (dimension.spans_multiple_cache_lines)
                {
                    score.streaming_memory += 0.25;
                }
            }

            const double contiguous_ratio = ratio(contiguous, known_contiguous);
            const double random_ratio = ratio(random, known_random);
            const double regular_stride_ratio = ratio(regular_stride, known_stride);

            if (known_contiguous > 0)
                score.streaming_memory += 1.2 * contiguous_ratio;
            if (known_random > 0)
                score.irregular_memory += 1.5 * random_ratio;
            if (known_stride > 0)
                score.streaming_memory += 0.6 * regular_stride_ratio;

            if (model.working_set_exceeds_l3)
            {
                score.streaming_memory += 1.1;
                score.irregular_memory += 0.35;
            }
            else if (model.l3_pressure > 0.0 && model.l3_pressure <= 0.50)
            {
                score.compute += 0.35;
            }

            if (analysis.objects_are_large)
                score.streaming_memory += 0.55;
            if (analysis.is_multidimensional)
                score.mixed += 0.30;
            if (analysis.has_many_iterations)
            {
                score.compute += 0.20;
                score.streaming_memory += 0.20;
            }

            if (profile != nullptr && profile->available)
            {
                result.used_profile_observations = true;
                const double cv = std::max(0.0, profile->coefficient_of_variation);
                const double tail = std::max(1.0, profile->tail_ratio);
                const double regional = std::max(1.0, profile->regional_cost_ratio);

                if (cv <= 0.10 && tail <= 1.20 && regional <= 1.20)
                {
                    score.compute += 0.35;
                    score.streaming_memory += 0.25;
                }
                else
                {
                    score.irregular_memory += std::min(1.2, cv * 1.8);
                    score.irregular_memory += std::min(0.8, (tail - 1.0) * 0.5);
                    score.mixed += std::min(0.8, (regional - 1.0) * 0.4);
                }
            }

            if (!hints_available)
            {
                if (random_ratio >= 0.50)
                    score.irregular_memory += 0.50;
                else if (contiguous_ratio >= 0.75 && model.l3_pressure >= 1.0)
                    score.streaming_memory += 0.60;
            }

            const std::array<double, 5> values{
                score.compute,
                score.streaming_memory,
                score.irregular_memory,
                score.branch,
                score.mixed};

            std::size_t best_index = 0;
            std::size_t second_index = 1;
            if (values[second_index] > values[best_index])
                std::swap(best_index, second_index);

            for (std::size_t index = 2; index < values.size(); ++index)
            {
                if (values[index] > values[best_index])
                {
                    second_index = best_index;
                    best_index = index;
                }
                else if (values[index] > values[second_index])
                {
                    second_index = index;
                }
            }

            const double best = values[best_index];
            const double second = values[second_index];
            const double total =
                score.compute + score.streaming_memory +
                score.irregular_memory + score.branch + score.mixed;

            if (best < 0.75 || total <= 0.0)
            {
                result.family = WorkloadFamily::Unknown;
                result.confidence = 0.0;
                result.ambiguous = true;
                return result;
            }

            const double margin = best - second;
            const double dominance = best / std::max(1.0e-9, total);
            const double margin_ratio = margin / std::max(1.0e-9, best);
            result.confidence = clamp01(0.55 * dominance + 0.45 * margin_ratio);
            result.ambiguous = margin_ratio < 0.18 || result.confidence < 0.35;

            static constexpr std::array<WorkloadFamily, 5> families{
                WorkloadFamily::ComputeHeavy,
                WorkloadFamily::StreamingMemory,
                WorkloadFamily::IrregularMemory,
                WorkloadFamily::BranchHeavy,
                WorkloadFamily::Mixed};

            result.family = result.ambiguous
                ? WorkloadFamily::Mixed
                : families[best_index];

            return result;
        }

    private:
        static double clamp01(double value)
        {
            if (!std::isfinite(value))
                return 0.0;
            return std::max(0.0, std::min(1.0, value));
        }

        static double ratio(std::size_t numerator, std::size_t denominator)
        {
            return denominator == 0
                ? 0.0
                : static_cast<double>(numerator) /
                      static_cast<double>(denominator);
        }
    };
}
