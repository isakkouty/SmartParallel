#pragma once

#include <smart/execution/parallel.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace smart
{
namespace detail
{
enum class ParallelAlgorithmKind : std::size_t
{
    ForEach = 0xA14001u,
    Transform = 0xA14002u,
    Copy = 0xA14003u,
    Fill = 0xA14004u,
    Generate = 0xA14005u,
    Reduce = 0xA14006u,
    TransformReduce = 0xA14007u,
    Count = 0xA14008u,
    CountIf = 0xA14009u,
    AnyOf = 0xA1400Au,
    AllOf = 0xA1400Bu,
    NoneOf = 0xA1400Cu,
    Find = 0xA1400Du,
    FindIf = 0xA1400Eu
};

enum class AlgorithmDispatchRoute : std::uint8_t
{
    Scheduled,
    Sequential
};

inline bool algorithm_hot_dispatch_family(ParallelAlgorithmKind kind) noexcept
{
    switch (kind)
    {
        case ParallelAlgorithmKind::Copy:
        case ParallelAlgorithmKind::Reduce:
        case ParallelAlgorithmKind::Count:
        case ParallelAlgorithmKind::AnyOf:
        case ParallelAlgorithmKind::AllOf:
        case ParallelAlgorithmKind::NoneOf:
        case ParallelAlgorithmKind::Find:
        case ParallelAlgorithmKind::FindIf:
            return true;
        default:
            return false;
    }
}

inline bool algorithm_search_or_predicate_family(ParallelAlgorithmKind kind) noexcept
{
    switch (kind)
    {
        case ParallelAlgorithmKind::AnyOf:
        case ParallelAlgorithmKind::AllOf:
        case ParallelAlgorithmKind::NoneOf:
        case ParallelAlgorithmKind::Find:
        case ParallelAlgorithmKind::FindIf:
            return true;
        default:
            return false;
    }
}

inline std::size_t algorithm_dispatch_policy_signature(const Config& config) noexcept
{
    std::size_t hash = parallel_policy_signature(config);
    hash = combine_hash(hash, config.enable_parallel_algorithm_hot_dispatch ? 1u : 0u);
    hash = combine_hash(hash, config.parallel_algorithm_hot_dispatch_max_entries);
    hash = combine_hash(
        hash, std::hash<double>{}(config.parallel_algorithm_hot_dispatch_minimum_parallel_speedup));
    hash = combine_hash(
        hash, std::hash<double>{}(config.parallel_algorithm_hot_dispatch_probe_max_ms));
    hash = combine_hash(hash, std::hash<double>{}(config.parallel_algorithm_hot_dispatch_blend));
    hash = combine_hash(hash, config.parallel_algorithm_hot_dispatch_revalidate_interval);
    hash = combine_hash(hash, config.parallel_algorithm_hot_dispatch_search_revalidate_interval);
    return hash;
}

struct AlgorithmDispatchKey
{
    ParallelAlgorithmKind kind = ParallelAlgorithmKind::Reduce;
    std::size_t callsite = 0;
    std::size_t element_size = 0;
    std::size_t iteration_bucket = 0;
    std::size_t byte_bucket = 0;
    std::size_t root_budget = 0;
    std::size_t policy_signature = 0;
    std::uint64_t profile_cache_epoch = 0;

    bool operator==(const AlgorithmDispatchKey& other) const noexcept
    {
        return kind == other.kind && callsite == other.callsite
               && element_size == other.element_size
               && iteration_bucket == other.iteration_bucket
               && byte_bucket == other.byte_bucket && root_budget == other.root_budget
               && policy_signature == other.policy_signature
               && profile_cache_epoch == other.profile_cache_epoch;
    }
};

struct AlgorithmDispatchKeyHasher
{
    std::size_t operator()(const AlgorithmDispatchKey& key) const noexcept
    {
        std::size_t hash = static_cast<std::size_t>(key.kind);
        hash = combine_hash(hash, key.callsite);
        hash = combine_hash(hash, key.element_size);
        hash = combine_hash(hash, key.iteration_bucket);
        hash = combine_hash(hash, key.byte_bucket);
        hash = combine_hash(hash, key.root_budget);
        hash = combine_hash(hash, key.policy_signature);
        hash = combine_hash(hash, static_cast<std::size_t>(key.profile_cache_epoch));
        return hash;
    }
};

struct AlgorithmDispatchSelection
{
    bool enabled = false;
    AlgorithmDispatchRoute route = AlgorithmDispatchRoute::Scheduled;
    bool probe = false;
    bool fresh_scheduler = false;
    std::uint64_t probe_nonce = 0;
    AlgorithmDispatchKey key{};
};

class AlgorithmDispatchCache
{
  public:
    AlgorithmDispatchSelection select(ParallelAlgorithmKind kind,
                                      std::size_t callsite,
                                      std::size_t total,
                                      std::size_t element_size)
    {
        const Config& config = effective_config();
        const ExecutionContext context = current_execution_context();
        if (!config.enable_parallel_algorithm_hot_dispatch
            || config.execution_engine != ExecutionEngineType::Auto || context.depth != 0
            || total == 0 || !algorithm_hot_dispatch_family(kind))
        {
            return {};
        }

        AlgorithmDispatchSelection selection;
        selection.enabled = true;
        selection.key = make_key(kind, callsite, total, element_size, config);

        Shard& shard = shards_[shard_index(selection.key)];
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto found = shard.entries.find(selection.key);
        if (found == shard.entries.end())
        {
            make_room_for_insert_locked(shard, configured_max_entries(config));
            found = shard.entries.emplace(selection.key, Entry{}).first;
        }
        Entry& entry = found->second;
        entry.last_access_epoch = ++shard.access_epoch;

        if (entry.probe_in_progress)
        {
            selection.route = entry.stable
                ? entry.stable_route
                : AlgorithmDispatchRoute::Scheduled;
            return selection;
        }

        if (!entry.stable)
        {
            if (entry.scheduled_samples == 0)
            {
                begin_probe(entry, selection, AlgorithmDispatchRoute::Scheduled, false);
                return selection;
            }

            if (entry.sequential_samples == 0)
            {
                if (entry.scheduled_ms <= config.parallel_algorithm_hot_dispatch_probe_max_ms)
                {
                    begin_probe(entry, selection, AlgorithmDispatchRoute::Sequential, false);
                    return selection;
                }

                entry.stable = true;
                entry.stable_route = AlgorithmDispatchRoute::Scheduled;
                entry.uses_since_revalidation = 0;
                selection.route = AlgorithmDispatchRoute::Scheduled;
                return selection;
            }

            choose_stable_route(entry, config);
        }

        const std::size_t interval = revalidation_interval(kind, config);
        if (interval > 0 && ++entry.uses_since_revalidation >= interval)
        {
            entry.uses_since_revalidation = 0;
            if (entry.stable_route == AlgorithmDispatchRoute::Sequential)
            {
                begin_probe(entry, selection, AlgorithmDispatchRoute::Scheduled, true);
                return selection;
            }
            if (entry.scheduled_ms <= config.parallel_algorithm_hot_dispatch_probe_max_ms)
            {
                begin_probe(entry, selection, AlgorithmDispatchRoute::Sequential, false);
                return selection;
            }
        }

        selection.route = entry.stable_route;
        return selection;
    }

    void complete(const AlgorithmDispatchSelection& selection, double elapsed_ms) noexcept
    {
        if (!selection.enabled || elapsed_ms < 0.0)
            return;

        Shard& shard = shards_[shard_index(selection.key)];
        std::lock_guard<std::mutex> lock(shard.mutex);
        const auto found = shard.entries.find(selection.key);
        if (found == shard.entries.end())
            return;

        Entry& entry = found->second;
        entry.last_access_epoch = ++shard.access_epoch;
        const double blend = bounded_blend(effective_config().parallel_algorithm_hot_dispatch_blend);
        if (selection.route == AlgorithmDispatchRoute::Sequential)
        {
            update_measurement(entry.sequential_ms, entry.sequential_samples, elapsed_ms, blend);
        }
        else
        {
            update_measurement(entry.scheduled_ms, entry.scheduled_samples, elapsed_ms, blend);
        }

        if (selection.probe && entry.probe_in_progress
            && entry.active_probe_nonce == selection.probe_nonce)
        {
            entry.probe_in_progress = false;
            entry.active_probe_nonce = 0;
            if (entry.scheduled_samples > 0 && entry.sequential_samples > 0)
                choose_stable_route(entry, effective_config());
            else if (entry.scheduled_samples > 0
                     && entry.scheduled_ms
                            > effective_config().parallel_algorithm_hot_dispatch_probe_max_ms)
            {
                entry.stable = true;
                entry.stable_route = AlgorithmDispatchRoute::Scheduled;
                entry.uses_since_revalidation = 0;
            }
        }
    }

    void cancel(const AlgorithmDispatchSelection& selection) noexcept
    {
        if (!selection.enabled || !selection.probe)
            return;
        Shard& shard = shards_[shard_index(selection.key)];
        std::lock_guard<std::mutex> lock(shard.mutex);
        const auto found = shard.entries.find(selection.key);
        if (found == shard.entries.end())
            return;
        Entry& entry = found->second;
        if (entry.probe_in_progress && entry.active_probe_nonce == selection.probe_nonce)
        {
            entry.probe_in_progress = false;
            entry.active_probe_nonce = 0;
        }
    }

    void clear()
    {
        for (Shard& shard : shards_)
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            shard.entries.clear();
            shard.access_epoch = 0;
        }
    }

  private:
    struct Entry
    {
        bool stable = false;
        AlgorithmDispatchRoute stable_route = AlgorithmDispatchRoute::Scheduled;
        double sequential_ms = 0.0;
        double scheduled_ms = 0.0;
        std::size_t sequential_samples = 0;
        std::size_t scheduled_samples = 0;
        std::size_t uses_since_revalidation = 0;
        bool probe_in_progress = false;
        std::uint64_t active_probe_nonce = 0;
        std::uint64_t last_access_epoch = 0;
    };

    struct Shard
    {
        std::mutex mutex;
        std::unordered_map<AlgorithmDispatchKey, Entry, AlgorithmDispatchKeyHasher> entries;
        std::uint64_t access_epoch = 0;
    };

    static constexpr std::size_t shard_count = 16;

    static AlgorithmDispatchKey make_key(ParallelAlgorithmKind kind,
                                         std::size_t callsite,
                                         std::size_t total,
                                         std::size_t element_size,
                                         const Config& config) noexcept
    {
        const std::size_t maximum = std::numeric_limits<std::size_t>::max();
        const std::size_t bytes = element_size != 0 && total > maximum / element_size
            ? maximum
            : total * element_size;
        return {kind,
                callsite,
                element_size,
                iteration_bucket(total),
                iteration_bucket(bytes),
                config.nested_root_concurrency_budget,
                algorithm_dispatch_policy_signature(config),
                global_function_profile_cache().cache_epoch()};
    }

    static std::size_t configured_max_entries(const Config& config) noexcept
    {
        return runtime_limits::bounded_limit(
            config.parallel_algorithm_hot_dispatch_max_entries,
            runtime_limits::algorithm_dispatch_entries);
    }

    static std::size_t revalidation_interval(ParallelAlgorithmKind kind,
                                             const Config& config) noexcept
    {
        return algorithm_search_or_predicate_family(kind)
            ? config.parallel_algorithm_hot_dispatch_search_revalidate_interval
            : config.parallel_algorithm_hot_dispatch_revalidate_interval;
    }

    static double bounded_blend(double blend) noexcept
    {
        return std::max(0.0, std::min(1.0, blend));
    }

    static void update_measurement(double& average,
                                   std::size_t& samples,
                                   double elapsed_ms,
                                   double blend) noexcept
    {
        if (samples == 0)
            average = elapsed_ms;
        else
            average = average * (1.0 - blend) + elapsed_ms * blend;
        if (samples != std::numeric_limits<std::size_t>::max())
            ++samples;
    }

    static void choose_stable_route(Entry& entry, const Config& config) noexcept
    {
        if (entry.sequential_samples == 0 || entry.scheduled_samples == 0)
            return;
        const double required_speedup = std::max(
            1.0, config.parallel_algorithm_hot_dispatch_minimum_parallel_speedup);
        entry.stable = true;
        entry.stable_route = entry.scheduled_ms > 0.0
                && entry.sequential_ms / entry.scheduled_ms >= required_speedup
            ? AlgorithmDispatchRoute::Scheduled
            : AlgorithmDispatchRoute::Sequential;
        entry.uses_since_revalidation = 0;
    }

    void begin_probe(Entry& entry,
                     AlgorithmDispatchSelection& selection,
                     AlgorithmDispatchRoute route,
                     bool fresh_scheduler) noexcept
    {
        const std::uint64_t nonce = next_probe_nonce_.fetch_add(1, std::memory_order_relaxed);
        entry.probe_in_progress = true;
        entry.active_probe_nonce = nonce;
        selection.route = route;
        selection.probe = true;
        selection.fresh_scheduler = fresh_scheduler;
        selection.probe_nonce = nonce;
    }

    static std::size_t shard_index(const AlgorithmDispatchKey& key) noexcept
    {
        return AlgorithmDispatchKeyHasher{}(key) % shard_count;
    }

    static void make_room_for_insert_locked(Shard& shard, std::size_t maximum_entries)
    {
        const std::size_t per_shard = std::max<std::size_t>(
            1, (maximum_entries + shard_count - 1) / shard_count);
        while (shard.entries.size() >= per_shard)
        {
            auto victim = shard.entries.end();
            for (auto it = shard.entries.begin(); it != shard.entries.end(); ++it)
            {
                if (it->second.probe_in_progress)
                    continue;
                if (victim == shard.entries.end()
                    || it->second.last_access_epoch < victim->second.last_access_epoch)
                    victim = it;
            }
            if (victim == shard.entries.end())
                break;
            shard.entries.erase(victim);
        }
    }

    std::array<Shard, shard_count> shards_{};
    std::atomic<std::uint64_t> next_probe_nonce_{1};
};

AlgorithmDispatchCache* active_runtime_algorithm_dispatch_cache() noexcept;

inline AlgorithmDispatchCache& global_algorithm_dispatch_cache()
{
    if (auto* cache = active_runtime_algorithm_dispatch_cache())
        return *cache;
    static AlgorithmDispatchCache cache;
    return cache;
}

inline void publish_algorithm_direct_sequential(double elapsed_ms) noexcept
{
    // Keep the thread-local diagnostics authoritative without reconstructing
    // the large DecisionReport object on every microsecond-scale cache hit.
    DecisionReport& report = global_last_decision_report();
    report.has_function_profile = false;
    report.plan = ExecutionPlan{};
    report.plan.engine = ExecutionEngineType::Auto;
    report.actual_execution_ms = elapsed_ms;

    ParallelForProfileDiagnostics& diagnostics =
        global_last_parallel_for_profile_diagnostics();
    diagnostics.profile_available = true;
    diagnostics.cache_hit = true;
    diagnostics.sequential_fast_path = true;
    diagnostics.sampled_iterations = 0;
    diagnostics.estimated_sequential_ms = elapsed_ms;
    diagnostics.estimated_parallel_ms = 0.0;
    diagnostics.predicted_speedup = 0.0;
    diagnostics.cache_lookup_ms = 0.0;
    diagnostics.workload_analysis_ms = 0.0;
    diagnostics.profiling_ms = 0.0;
    diagnostics.decision_ms = 0.0;
    diagnostics.execution_ms = elapsed_ms;
    diagnostics.total_ms = elapsed_ms;
}

inline std::size_t algorithm_dispatch_scheduler_callsite(
    std::size_t base_callsite,
    const AlgorithmDispatchSelection& selection) noexcept
{
    if (!selection.fresh_scheduler)
        return base_callsite;
    std::size_t hash = combine_hash(base_callsite, 0xA140484F54505242ull);
    hash = combine_hash(hash, static_cast<std::size_t>(selection.probe_nonce));
    return hash;
}

template <typename SequentialFunction, typename ScheduledFunction>
auto run_hot_dispatched_algorithm(ParallelAlgorithmKind kind,
                                  std::size_t callsite,
                                  std::size_t total,
                                  std::size_t element_size,
                                  SequentialFunction&& sequential_function,
                                  ScheduledFunction&& scheduled_function)
    -> std::invoke_result_t<SequentialFunction&>
{
    using Result = std::invoke_result_t<SequentialFunction&>;
    static_assert(std::is_same_v<Result,
                                 std::invoke_result_t<ScheduledFunction&, std::size_t>>,
                  "SmartParallel hot dispatch routes must return the same type");

    AlgorithmDispatchSelection selection = global_algorithm_dispatch_cache().select(
        kind, callsite, total, element_size);
    if (!selection.enabled)
        return scheduled_function(callsite);

    const auto start = std::chrono::steady_clock::now();
    try
    {
        if constexpr (std::is_void_v<Result>)
        {
            if (selection.route == AlgorithmDispatchRoute::Sequential)
                sequential_function();
            else
                scheduled_function(algorithm_dispatch_scheduler_callsite(callsite, selection));
            const double elapsed_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            global_algorithm_dispatch_cache().complete(selection, elapsed_ms);
            if (selection.route == AlgorithmDispatchRoute::Sequential)
                publish_algorithm_direct_sequential(elapsed_ms);
        }
        else
        {
            Result result = selection.route == AlgorithmDispatchRoute::Sequential
                ? sequential_function()
                : scheduled_function(algorithm_dispatch_scheduler_callsite(callsite, selection));
            const double elapsed_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            global_algorithm_dispatch_cache().complete(selection, elapsed_ms);
            if (selection.route == AlgorithmDispatchRoute::Sequential)
                publish_algorithm_direct_sequential(elapsed_ms);
            return result;
        }
    }
    catch (...)
    {
        global_algorithm_dispatch_cache().cancel(selection);
        throw;
    }
}
} // namespace detail
} // namespace smart
