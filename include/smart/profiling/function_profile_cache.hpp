#pragma once

#include <cstddef>
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

    class FunctionProfileCache
    {
    public:
        std::optional<FunctionProfile> find(const FunctionProfileKey& key) const
        {
            auto it = profiles_.find(key);

            if (it == profiles_.end())
                return std::nullopt;

            return it->second;
        }

        void store(const FunctionProfileKey& key, const FunctionProfile& profile)
        {
            profiles_[key] = profile;
        }

        void clear()
        {
            profiles_.clear();
        }

        std::size_t size() const
        {
            return profiles_.size();
        }

    private:
        std::unordered_map<
            FunctionProfileKey,
            FunctionProfile,
            FunctionProfileKeyHasher
        > profiles_;
    };

    inline FunctionProfileCache& global_function_profile_cache()
    {
        static FunctionProfileCache cache;
        return cache;
    }

    inline std::size_t iteration_bucket(std::size_t iterations)
    {
        if (iterations == 0)
            return 0;

        std::size_t bucket = 1;

        while (bucket < iterations)
        {
            bucket *= 2;
        }

        return bucket;
    }
}
