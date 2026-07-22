#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>

#include <smart/core/config.hpp>
#include <smart/execution/runtime_capabilities.hpp>
#include <smart/profiling/function_profile_cache.hpp>

namespace smart
{
class BackendCalibrationCache
{
  public:
    ExecutionEngineType select(const FunctionProfileKey& key,
                               std::uint64_t profile_generation,
                               ExecutionEngineType requested)
    {
        const Config& config = global_config();
        if (!config.enable_parallel_for_backend_calibration
            || profile_generation == 0
            || !eligible_backend(requested))
            return requested;

        const ExecutionEngineType alternative = other_backend(requested);
        if (!execution_backend_available(alternative))
            return requested;

        std::lock_guard<std::mutex> lock(mutex_);
        Entry& entry = find_or_create_unlocked(key);
        touch_unlocked(entry);
        if (entry.profile_generation != profile_generation)
        {
            entry = Entry{};
            entry.profile_generation = profile_generation;
            touch_unlocked(entry);
        }

        if (entry.winner.has_value())
            return *entry.winner;

        const std::size_t minimum = std::max<std::size_t>(
            1, config.parallel_for_backend_calibration_min_samples);
        const Statistics& requested_stats = stats(entry, requested);
        const Statistics& alternative_stats = stats(entry, alternative);

        if (requested_stats.samples < minimum)
            return requested;
        if (alternative_stats.samples < minimum)
            return alternative;

        entry.winner = choose_winner_unlocked(entry, requested, alternative);
        return *entry.winner;
    }

    void record(const FunctionProfileKey& key,
                std::uint64_t profile_generation,
                ExecutionEngineType backend,
                double elapsed_ms)
    {
        const Config& config = global_config();
        if (!config.enable_parallel_for_backend_calibration
            || profile_generation == 0
            || !eligible_backend(backend)
            || !(elapsed_ms >= 0.0)
            || !std::isfinite(elapsed_ms))
            return;

        const ExecutionEngineType alternative = other_backend(backend);
        if (!execution_backend_available(alternative))
            return;

        std::lock_guard<std::mutex> lock(mutex_);
        Entry& entry = find_or_create_unlocked(key);
        if (entry.profile_generation != profile_generation)
        {
            entry = Entry{};
            entry.profile_generation = profile_generation;
        }
        touch_unlocked(entry);
        Statistics& observation = stats(entry, backend);
        ++observation.samples;
        observation.mean_ms +=
            (elapsed_ms - observation.mean_ms) / static_cast<double>(observation.samples);

        const std::size_t minimum = std::max<std::size_t>(
            1, config.parallel_for_backend_calibration_min_samples);
        const Statistics& thread_pool = stats(entry, ExecutionEngineType::ThreadPool);
        const Statistics& one_tbb = stats(entry, ExecutionEngineType::OneTbb);
        if (thread_pool.samples >= minimum && one_tbb.samples >= minimum)
        {
            entry.winner = choose_winner_unlocked(
                entry, ExecutionEngineType::ThreadPool, ExecutionEngineType::OneTbb);
        }
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
        access_clock_ = 0;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

  private:
    struct Statistics
    {
        std::size_t samples = 0;
        double mean_ms = 0.0;
    };

    struct Entry
    {
        std::uint64_t profile_generation = 0;
        Statistics thread_pool;
        Statistics one_tbb;
        std::optional<ExecutionEngineType> winner;
        std::uint64_t last_access = 0;
    };

    static bool eligible_backend(ExecutionEngineType backend) noexcept
    {
        return backend == ExecutionEngineType::ThreadPool
            || backend == ExecutionEngineType::OneTbb;
    }

    static ExecutionEngineType other_backend(ExecutionEngineType backend) noexcept
    {
        return backend == ExecutionEngineType::OneTbb
            ? ExecutionEngineType::ThreadPool
            : ExecutionEngineType::OneTbb;
    }

    static Statistics& stats(Entry& entry, ExecutionEngineType backend) noexcept
    {
        return backend == ExecutionEngineType::OneTbb ? entry.one_tbb : entry.thread_pool;
    }

    static const Statistics& stats(const Entry& entry, ExecutionEngineType backend) noexcept
    {
        return backend == ExecutionEngineType::OneTbb ? entry.one_tbb : entry.thread_pool;
    }

    ExecutionEngineType choose_winner_unlocked(const Entry& entry,
                                                ExecutionEngineType first,
                                                ExecutionEngineType second) const noexcept
    {
        const Statistics& first_stats = stats(entry, first);
        const Statistics& second_stats = stats(entry, second);
        const double hysteresis = std::max(
            0.0, global_config().parallel_for_backend_calibration_hysteresis_percent) / 100.0;
        if (second_stats.mean_ms < first_stats.mean_ms * (1.0 - hysteresis))
            return second;
        if (first_stats.mean_ms < second_stats.mean_ms * (1.0 - hysteresis))
            return first;
        // Inside the hysteresis band prefer the persistent ThreadPool. It has
        // the smallest integration surface and avoids backend oscillation.
        return ExecutionEngineType::ThreadPool;
    }

    Entry& find_or_create_unlocked(const FunctionProfileKey& key)
    {
        auto found = entries_.find(key);
        if (found != entries_.end())
            return found->second;

        const std::size_t maximum = runtime_limits::bounded_limit(
            global_config().parallel_for_backend_calibration_max_entries,
            runtime_limits::backend_calibration_states);
        if (entries_.size() >= maximum && !entries_.empty())
        {
            auto oldest = entries_.begin();
            for (auto it = std::next(entries_.begin()); it != entries_.end(); ++it)
            {
                if (it->second.last_access < oldest->second.last_access)
                    oldest = it;
            }
            entries_.erase(oldest);
        }
        return entries_.emplace(key, Entry{}).first->second;
    }

    void touch_unlocked(Entry& entry) noexcept
    {
        entry.last_access = ++access_clock_;
    }

    mutable std::mutex mutex_;
    std::unordered_map<FunctionProfileKey, Entry, FunctionProfileKeyHasher> entries_;
    std::uint64_t access_clock_ = 0;
};

inline BackendCalibrationCache& global_backend_calibration_cache()
{
    static BackendCalibrationCache cache;
    return cache;
}
} // namespace smart
