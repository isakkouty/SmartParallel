#pragma once

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>

#include <smart/profiling/function_profiler.hpp>

namespace smart
{
    struct FunctionProfileKey
    {
        std::size_t function_hash = 0;
        std::size_t element_size = 0;
        std::size_t iteration_bucket = 0;

        bool operator==(const FunctionProfileKey& other) const
        {
            return function_hash == other.function_hash &&
                   element_size == other.element_size &&
                   iteration_bucket == other.iteration_bucket;
        }
    };

    struct FunctionProfileKeyHasher
    {
        std::size_t operator()(const FunctionProfileKey& key) const
        {
            std::size_t hash = key.function_hash;
            hash ^= key.element_size + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= key.iteration_bucket + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    struct CachedFunctionProfile
    {
        FunctionProfile profile;
        std::size_t hits = 0;
        std::size_t observations = 0;
        std::size_t sequential_fast_path_uses = 0;
    };

    class FunctionProfileCache
    {
    public:
        std::optional<CachedFunctionProfile> find(const FunctionProfileKey& key)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = profiles_.find(key);
            if (it == profiles_.end()) return std::nullopt;
            ++it->second.hits;
            return it->second;
        }

        void note_sequential_fast_path_use(const FunctionProfileKey& key)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = profiles_.find(key);
            if (it != profiles_.end()) ++it->second.sequential_fast_path_uses;
        }

        void store(const FunctionProfileKey& key, const FunctionProfile& profile,
                   double blend = 0.25, double classification_threshold = 1.10,
                   std::size_t high_confidence_observations = 3)
        {
            if (!profile.available) return;
            std::lock_guard<std::mutex> lock(mutex_);
            auto& entry = profiles_[key];
            if (!entry.profile.available)
            {
                entry.profile = profile;
                entry.observations = 1;
                entry.sequential_fast_path_uses = 0;
                return;
            }

            const bool old_parallel = entry.profile.parallel_worthiness >= classification_threshold;
            const bool new_parallel = profile.parallel_worthiness >= classification_threshold;
            if (old_parallel != new_parallel)
            {
                // A fresh observation contradicts the cached classification.
                // Replace it immediately instead of slowly blending through a
                // stale state; confidence must be rebuilt from new observations.
                entry.profile = profile;
                entry.observations = 1;
                entry.sequential_fast_path_uses = 0;
                return;
            }

            blend = std::max(0.0, std::min(1.0, blend));
            auto mix = [blend](double old_value, double new_value)
            { return old_value * (1.0 - blend) + new_value * blend; };
            entry.profile.avg_ms_per_iteration = mix(entry.profile.avg_ms_per_iteration, profile.avg_ms_per_iteration);
            entry.profile.median_ms_per_iteration = mix(entry.profile.median_ms_per_iteration, profile.median_ms_per_iteration);
            entry.profile.trimmed_mean_ms_per_iteration = mix(entry.profile.trimmed_mean_ms_per_iteration, profile.trimmed_mean_ms_per_iteration);
            entry.profile.estimated_total_work_ms = profile.estimated_total_work_ms;
            entry.profile.parallel_worthiness = profile.parallel_worthiness;
            entry.profile.samples += profile.samples;
            ++entry.observations;
            entry.sequential_fast_path_uses = 0;
            entry.profile.metadata.confidence = entry.observations >= std::max<std::size_t>(1, high_confidence_observations)
                ? ObservationConfidence::High : ObservationConfidence::Medium;
        }

        void clear() { std::lock_guard<std::mutex> lock(mutex_); profiles_.clear(); }
        std::size_t size() const { std::lock_guard<std::mutex> lock(mutex_); return profiles_.size(); }

    private:
        mutable std::mutex mutex_;
        std::unordered_map<FunctionProfileKey, CachedFunctionProfile, FunctionProfileKeyHasher> profiles_;
    };

    inline FunctionProfileCache& global_function_profile_cache()
    {
        static FunctionProfileCache cache;
        return cache;
    }

    inline std::size_t iteration_bucket(std::size_t iterations)
    {
        if (iterations == 0) return 0;
        std::size_t bucket = 1;
        while (bucket < iterations && bucket <= static_cast<std::size_t>(-1) / 2) bucket *= 2;
        return bucket;
    }
}
