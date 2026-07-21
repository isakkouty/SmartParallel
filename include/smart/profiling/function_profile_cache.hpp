#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <smart/core/config.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/profiling/function_profiler.hpp>
#include <unordered_map>
#include <unordered_set>

namespace smart
{
struct FunctionProfileKey
{
    std::size_t function_hash = 0;
    std::size_t element_size = 0;
    std::size_t iteration_bucket = 0;
    std::size_t depth = 0;
    std::size_t parent_callsite_hash = 0;
    std::size_t concurrency_budget = 1;
    ExecutionEngineType engine = ExecutionEngineType::Auto;
    // Separates profiles created under materially different scheduler policy
    // settings without requiring callers to clear the process-wide cache.
    std::size_t policy_signature = 0;

    bool operator==(const FunctionProfileKey& other) const noexcept
    {
        return function_hash == other.function_hash && element_size == other.element_size
               && iteration_bucket == other.iteration_bucket && depth == other.depth
               && parent_callsite_hash == other.parent_callsite_hash
               && concurrency_budget == other.concurrency_budget && engine == other.engine
               && policy_signature == other.policy_signature;
    }
};

struct FunctionProfileKeyHasher
{
    std::size_t operator()(const FunctionProfileKey& key) const noexcept
    {
        std::size_t hash = key.function_hash;
        const auto combine = [&hash](std::size_t value)
        {
            hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        };
        combine(key.element_size);
        combine(key.iteration_bucket);
        combine(key.depth);
        combine(key.parent_callsite_hash);
        combine(key.concurrency_budget);
        combine(static_cast<std::size_t>(key.engine));
        combine(key.policy_signature);
        return hash;
    }
};

struct CachedFunctionProfile
{
    FunctionProfile profile;
    std::size_t hits = 0;
    std::size_t observations = 0;
    std::size_t sequential_fast_path_uses = 0;
    bool observed_nested_calls = false;
    // Exponentially decayed recent evidence. This prevents one historical
    // nested observation from classifying a phase-changing callsite forever.
    double nested_call_frequency = 0.0;
    double nested_child_ms = 0.0;
    std::size_t nested_child_calls = 0;
    bool stable_plan_available = false;
    std::size_t stable_plan_uses = 0;
    ExecutionPlan stable_plan;
    std::uint64_t last_access_epoch = 0;
    std::uint64_t last_observation_group = 0;
    // Monotonic profile version. Stable plans may only be installed against
    // the exact profile generation used to compute them.
    std::uint64_t generation = 0;
    std::chrono::steady_clock::time_point last_profile_update{};
};

class FunctionProfileCache
{
  public:
    class ProfileBuildGuard
    {
      public:
        ProfileBuildGuard() = default;
        ProfileBuildGuard(const ProfileBuildGuard&) = delete;
        ProfileBuildGuard& operator=(const ProfileBuildGuard&) = delete;

        ProfileBuildGuard(ProfileBuildGuard&& other) noexcept
            : owner_(other.owner_), key_(other.key_), active_(other.active_)
        {
            other.owner_ = nullptr;
            other.active_ = false;
        }

        ProfileBuildGuard& operator=(ProfileBuildGuard&& other) noexcept
        {
            if (this == &other)
                return *this;
            release();
            owner_ = other.owner_;
            key_ = other.key_;
            active_ = other.active_;
            other.owner_ = nullptr;
            other.active_ = false;
            return *this;
        }

        ~ProfileBuildGuard() { release(); }

        bool owns_build() const noexcept { return active_; }

      private:
        friend class FunctionProfileCache;
        ProfileBuildGuard(FunctionProfileCache* owner, const FunctionProfileKey& key)
            : owner_(owner), key_(key), active_(owner != nullptr)
        {
        }

        void release() noexcept
        {
            if (owner_ != nullptr && active_)
                owner_->finish_profile_build(key_);
            owner_ = nullptr;
            active_ = false;
        }

        FunctionProfileCache* owner_ = nullptr;
        FunctionProfileKey key_{};
        bool active_ = false;
    };

    class RevalidationGuard
    {
      public:
        RevalidationGuard() = default;
        RevalidationGuard(const RevalidationGuard&) = delete;
        RevalidationGuard& operator=(const RevalidationGuard&) = delete;

        RevalidationGuard(RevalidationGuard&& other) noexcept
            : owner_(other.owner_), key_(other.key_), active_(other.active_)
        {
            other.owner_ = nullptr;
            other.active_ = false;
        }

        RevalidationGuard& operator=(RevalidationGuard&& other) noexcept
        {
            if (this == &other)
                return *this;
            release();
            owner_ = other.owner_;
            key_ = other.key_;
            active_ = other.active_;
            other.owner_ = nullptr;
            other.active_ = false;
            return *this;
        }

        ~RevalidationGuard() { release(); }

        bool owns_revalidation() const noexcept { return active_; }

      private:
        friend class FunctionProfileCache;
        RevalidationGuard(FunctionProfileCache* owner, const FunctionProfileKey& key)
            : owner_(owner), key_(key), active_(owner != nullptr)
        {
        }

        void release() noexcept
        {
            if (owner_ != nullptr && active_)
                owner_->finish_revalidation(key_);
            owner_ = nullptr;
            active_ = false;
        }

        FunctionProfileCache* owner_ = nullptr;
        FunctionProfileKey key_{};
        bool active_ = false;
    };

    ProfileBuildGuard try_acquire_profile_build(const FunctionProfileKey& key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (profiles_.find(key) != profiles_.end())
            return {};
        const auto inserted = profile_builds_in_flight_.insert(key).second;
        return inserted ? ProfileBuildGuard(this, key) : ProfileBuildGuard{};
    }

    RevalidationGuard try_acquire_revalidation(const FunctionProfileKey& key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (profiles_.find(key) == profiles_.end())
            return {};
        const auto inserted = revalidations_in_flight_.insert(key).second;
        return inserted ? RevalidationGuard(this, key) : RevalidationGuard{};
    }

    std::optional<CachedFunctionProfile> find(const FunctionProfileKey& key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = profiles_.find(key);
        if (it == profiles_.end())
            return std::nullopt;
        saturating_increment(it->second.hits);
        touch(it->second);
        return it->second;
    }

    void note_sequential_fast_path_use(const FunctionProfileKey& key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = profiles_.find(key);
        if (it != profiles_.end())
        {
            saturating_increment(it->second.sequential_fast_path_uses);
            touch(it->second);
        }
    }

    std::uint64_t store(const FunctionProfileKey& key,
                        const FunctionProfile& profile,
                        double blend = 0.25,
                        double classification_threshold = 1.10,
                        std::size_t high_confidence_observations = 3,
                        std::size_t nested_child_calls = 0,
                        double nested_child_ms = 0.0,
                        std::uint64_t observation_group = 0,
                        std::uint64_t expected_cache_epoch = 0)
    {
        if (!profile.available)
            return 0;

        std::lock_guard<std::mutex> lock(mutex_);
        if (expected_cache_epoch != 0 && expected_cache_epoch != cache_epoch_)
            return 0;
        if (!make_room_for(key))
            return 0;
        auto& entry = profiles_[key];
        touch(entry);

        const auto publish_generation = [this](CachedFunctionProfile& target)
        {
            if (profile_generation_epoch_ == std::numeric_limits<std::uint64_t>::max())
                profile_generation_epoch_ = 1;
            else
                ++profile_generation_epoch_;
            target.generation = profile_generation_epoch_;
            target.last_profile_update = std::chrono::steady_clock::now();
            return target.generation;
        };

        const double nested_blend = std::clamp(
            global_config().parallel_for_profile_nested_evidence_blend, 0.0, 1.0);
        const double nested_threshold = std::clamp(
            global_config().parallel_for_profile_nested_evidence_threshold, 0.0, 1.0);
        const double nested_observation = nested_child_calls > 0 ? 1.0 : 0.0;

        if (!entry.profile.available)
        {
            entry.profile = profile;
            entry.observations = 1;
            entry.sequential_fast_path_uses = 0;
            entry.nested_call_frequency = nested_observation;
            entry.observed_nested_calls = entry.nested_call_frequency >= nested_threshold;
            entry.nested_child_calls = nested_child_calls;
            entry.nested_child_ms = nested_child_ms;
            entry.stable_plan_available = false;
            entry.stable_plan_uses = 0;
            entry.last_observation_group = observation_group;
            return publish_generation(entry);
        }

        const bool old_parallel = entry.profile.parallel_worthiness >= classification_threshold;
        const bool new_parallel = profile.parallel_worthiness >= classification_threshold;
        if (old_parallel != new_parallel)
        {
            // Contradictory evidence invalidates the current optimization state
            // immediately. Confidence and structural evidence are rebuilt from
            // the new workload phase instead of blending through stale data.
            entry.profile = profile;
            entry.observations = 1;
            entry.sequential_fast_path_uses = 0;
            entry.nested_call_frequency = nested_observation;
            entry.observed_nested_calls = entry.nested_call_frequency >= nested_threshold;
            entry.nested_child_calls = nested_child_calls;
            entry.nested_child_ms = nested_child_ms;
            entry.stable_plan_available = false;
            entry.stable_plan_uses = 0;
            entry.last_observation_group = observation_group;
            return publish_generation(entry);
        }

        blend = std::clamp(blend, 0.0, 1.0);
        const auto mix = [blend](double old_value, double new_value)
        {
            return old_value * (1.0 - blend) + new_value * blend;
        };
        entry.profile.avg_ms_per_iteration =
            mix(entry.profile.avg_ms_per_iteration, profile.avg_ms_per_iteration);
        entry.profile.median_ms_per_iteration =
            mix(entry.profile.median_ms_per_iteration, profile.median_ms_per_iteration);
        entry.profile.trimmed_mean_ms_per_iteration =
            mix(entry.profile.trimmed_mean_ms_per_iteration, profile.trimmed_mean_ms_per_iteration);
        entry.profile.estimated_total_work_ms = profile.estimated_total_work_ms;
        entry.profile.parallel_worthiness = profile.parallel_worthiness;
        entry.profile.samples = saturating_add(entry.profile.samples, profile.samples);
        const bool independent_observation = observation_group == 0
            || entry.last_observation_group == 0
            || entry.last_observation_group != observation_group;
        if (independent_observation)
        {
            saturating_increment(entry.observations);
            entry.last_observation_group = observation_group;
        }
        entry.sequential_fast_path_uses = 0;
        entry.nested_call_frequency =
            entry.nested_call_frequency * (1.0 - nested_blend)
            + nested_observation * nested_blend;
        entry.observed_nested_calls = entry.nested_call_frequency >= nested_threshold;
        entry.nested_child_calls = nested_child_calls;
        entry.nested_child_ms = mix(entry.nested_child_ms, nested_child_ms);
        entry.stable_plan_available = false;
        entry.stable_plan_uses = 0;
        entry.profile.metadata.confidence =
            entry.observations >= std::max<std::size_t>(1, high_confidence_observations)
                ? ObservationConfidence::High
                : ObservationConfidence::Medium;
        return publish_generation(entry);
    }

    bool store_stable_plan(const FunctionProfileKey& key,
                           const ExecutionPlan& plan,
                           std::uint64_t expected_generation = 0)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = profiles_.find(key);
        if (it == profiles_.end())
            return false;
        // Stable plans are optimization state derived from a specific profile.
        // Generation zero is never a valid publication token; accepting it as
        // a wildcard would allow a plan computed before cache invalidation to
        // attach to a newer profile for the same key.
        if (expected_generation == 0 || it->second.generation != expected_generation)
            return false;
        it->second.stable_plan = plan;
        it->second.stable_plan_available = true;
        it->second.stable_plan_uses = 0;
        touch(it->second);
        return true;
    }

    void note_stable_plan_use(const FunctionProfileKey& key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = profiles_.find(key);
        if (it != profiles_.end() && it->second.stable_plan_available)
        {
            saturating_increment(it->second.stable_plan_uses);
            touch(it->second);
        }
    }

    std::uint64_t cache_epoch() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_epoch_;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        profiles_.clear();
        // Do not clear in-flight ownership sets. Their RAII guards may still
        // exist on other threads. Removing the markers here would permit a new
        // guard for the same key, after which destruction of the old guard
        // could erase the new marker (an ABA-style ownership loss). Existing
        // operations are rejected by cache_epoch_ and release their markers
        // normally when their guards leave scope.
        access_epoch_ = 0;
        if (cache_epoch_ == std::numeric_limits<std::uint64_t>::max())
            cache_epoch_ = 1;
        else
            ++cache_epoch_;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return profiles_.size();
    }

  private:
    static void saturating_increment(std::size_t& value) noexcept
    {
        if (value != std::numeric_limits<std::size_t>::max())
            ++value;
    }

    static std::size_t saturating_add(std::size_t left, std::size_t right) noexcept
    {
        const std::size_t maximum = std::numeric_limits<std::size_t>::max();
        return right > maximum - left ? maximum : left + right;
    }

    void touch(CachedFunctionProfile& entry) noexcept
    {
        if (access_epoch_ == std::numeric_limits<std::uint64_t>::max())
        {
            // Epoch wrap is extraordinarily unlikely, but keeping ordering
            // defined costs little and avoids an eventual LRU inversion.
            std::uint64_t epoch = 1;
            for (auto& item : profiles_)
                item.second.last_access_epoch = epoch++;
            access_epoch_ = epoch;
        }
        entry.last_access_epoch = ++access_epoch_;
    }

    bool make_room_for(const FunctionProfileKey& incoming_key)
    {
        const std::size_t maximum = runtime_limits::bounded_limit(
            global_config().parallel_for_profile_cache_max_entries,
            runtime_limits::profile_cache_entries);
        if (profiles_.find(incoming_key) != profiles_.end())
            return true;

        while (profiles_.size() >= maximum)
        {
            auto victim = profiles_.end();
            for (auto it = profiles_.begin(); it != profiles_.end(); ++it)
            {
                if (profile_builds_in_flight_.find(it->first) != profile_builds_in_flight_.end()
                    || revalidations_in_flight_.find(it->first) != revalidations_in_flight_.end())
                    continue;
                if (victim == profiles_.end()
                    || it->second.last_access_epoch < victim->second.last_access_epoch)
                    victim = it;
            }
            if (victim == profiles_.end())
                return false; // preserve active entries and reject this publication
            profiles_.erase(victim);
        }
        return true;
    }

    void finish_profile_build(const FunctionProfileKey& key) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        profile_builds_in_flight_.erase(key);
    }

    void finish_revalidation(const FunctionProfileKey& key) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        revalidations_in_flight_.erase(key);
    }

    mutable std::mutex mutex_;
    std::unordered_map<FunctionProfileKey, CachedFunctionProfile, FunctionProfileKeyHasher>
        profiles_;
    std::unordered_set<FunctionProfileKey, FunctionProfileKeyHasher> profile_builds_in_flight_;
    std::unordered_set<FunctionProfileKey, FunctionProfileKeyHasher> revalidations_in_flight_;
    std::uint64_t access_epoch_ = 0;
    std::uint64_t profile_generation_epoch_ = 0;
    std::uint64_t cache_epoch_ = 1;
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
    while (bucket < iterations && bucket <= static_cast<std::size_t>(-1) / 2)
        bucket *= 2;
    return bucket;
}
} // namespace smart
