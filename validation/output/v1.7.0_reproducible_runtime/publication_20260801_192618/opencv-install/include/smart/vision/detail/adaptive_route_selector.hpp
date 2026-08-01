#pragma once

#include <smart/vision/execution_route.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace smart::vision::detail
{
struct RouteProfileKey
{
    std::uint64_t operation = 0;
    std::size_t pixel_bucket = 0;
    std::size_t width_bucket = 0;
    std::size_t height_bucket = 0;
    std::size_t stride_bucket = 0;
    std::size_t parameter_hash = 0;
    std::size_t worker_budget = 1;
    std::size_t candidate_mask = 0;
    std::size_t policy_generation = 0;
    std::size_t provider_fingerprint = 0;

    bool operator==(const RouteProfileKey& other) const noexcept
    {
        return operation == other.operation && pixel_bucket == other.pixel_bucket
            && width_bucket == other.width_bucket && height_bucket == other.height_bucket
            && stride_bucket == other.stride_bucket && parameter_hash == other.parameter_hash
            && worker_budget == other.worker_budget && candidate_mask == other.candidate_mask
            && policy_generation == other.policy_generation
            && provider_fingerprint == other.provider_fingerprint;
    }
};

struct RouteProfileKeyHasher
{
    std::size_t operator()(const RouteProfileKey& key) const noexcept
    {
        auto combine = [](std::size_t seed, std::size_t value)
        {
            return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u));
        };
        std::size_t hash = static_cast<std::size_t>(key.operation);
        hash = combine(hash, key.pixel_bucket);
        hash = combine(hash, key.width_bucket);
        hash = combine(hash, key.height_bucket);
        hash = combine(hash, key.stride_bucket);
        hash = combine(hash, key.parameter_hash);
        hash = combine(hash, key.worker_budget);
        hash = combine(hash, key.candidate_mask);
        hash = combine(hash, key.policy_generation);
        hash = combine(hash, key.provider_fingerprint);
        return hash;
    }
};

struct RouteSelectorSettings
{
    std::size_t maximum_entries = 512;
    double equivalence_ratio = 1.05;
    double absolute_equivalence_ms = 0.001;
    // Retained for source compatibility with v1.5 preview configurations.
    double measurement_blend = 0.25;
    std::size_t warmup_samples = 2;
    std::size_t minimum_samples = 3;
    std::size_t sample_window = 11;
    std::size_t holdout_samples = 2;
    std::size_t maximum_verification_failures = 2;
    std::size_t initial_revalidate_interval = 8;
    std::size_t revalidate_interval = 128;
    std::size_t drift_required_samples = 2;
    double drift_ratio = 1.25;
    double drift_absolute_ms = 0.002;
    bool force_revalidation = false;
    bool force_stable_sample = false;
};

struct RouteSelection
{
    bool enabled = false;
    bool cache_hit = false;
    bool stable = false;
    bool probe = false;
    bool warmup = false;
    bool holdout = false;
    bool revalidation = false;
    bool drift_sample = false;
    bool revalidation_stable_sample = false;
    std::uint8_t revalidation_stage = 0;
    std::uint64_t probe_nonce = 0;
    ExecutionRoute route = ExecutionRoute::NativeSequential;
    RouteProfileKey key{};
};

struct RouteMeasurementSnapshot
{
    ExecutionRoute stable_route = ExecutionRoute::NativeSequential;
    ExecutionRoute provisional_route = ExecutionRoute::NativeSequential;
    bool stable = false;
    bool holdout_active = false;
    bool revalidation_active = false;
    bool drift_detected = false;
    std::size_t use_count = 0;
    std::size_t revalidate_after_uses = 0;
    std::size_t verification_failures = 0;
    std::size_t route_switch_count = 0;
    std::size_t drift_strikes = 0;
    ExecutionRoute revalidation_challenger = ExecutionRoute::NativeSequential;
    double training_baseline_ms = 0.0;
    double current_baseline_ms = 0.0;
    double last_revalidation_stable_ms = 0.0;
    double last_revalidation_challenger_ms = 0.0;
    std::vector<ExecutionRoute> routes;
    std::vector<double> elapsed_ms;
    std::vector<double> mad_ms;
    std::vector<double> minimum_ms;
    std::vector<double> maximum_ms;
    std::vector<double> current_elapsed_ms;
    std::vector<std::size_t> samples;
    std::vector<std::size_t> current_samples;
    std::vector<std::size_t> warmups;
    std::vector<std::size_t> holdout_samples;
    std::vector<bool> active;
};

class AdaptiveRouteSelector
{
  public:
    RouteSelection select(const RouteProfileKey& key,
                          const std::vector<ExecutionRoute>& candidates,
                          const RouteSelectorSettings& settings)
    {
        RouteSelection selection;
        selection.enabled = !candidates.empty();
        selection.key = key;
        if (candidates.empty())
            return selection;

        Shard& shard = shards_[shard_index(key)];
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto found = shard.entries.find(key);
        selection.cache_hit = found != shard.entries.end();
        if (found == shard.entries.end())
        {
            make_room_locked(shard, normalized_maximum(settings.maximum_entries));
            Entry entry;
            reset_entry(entry, candidates, key, settings);
            found = shard.entries.emplace(key, std::move(entry)).first;
        }

        Entry& entry = found->second;
        entry.last_access = ++shard.access_epoch;
        if (entry.candidates != candidates)
            reset_entry(entry, candidates, key, settings);

        if (entry.probe_in_progress)
        {
            selection.route = entry.stable ? entry.stable_route
                : entry.phase == Phase::Holdout ? entry.candidates[entry.provisional_index]
                : entry.candidates.front();
            selection.stable = entry.stable;
            return selection;
        }

        const std::size_t warmup_index = next_warmup_index(entry, settings);
        if (warmup_index != invalid_index())
        {
            begin_probe(entry, selection, warmup_index, true, false, false);
            return selection;
        }

        if (!entry.stable)
        {
            prepare_learning_state(entry, settings);
            if (entry.phase == Phase::Holdout)
            {
                const std::size_t holdout_index = next_holdout_index(entry, settings);
                if (holdout_index != invalid_index())
                {
                    begin_probe(entry, selection, holdout_index, false, true, false);
                    return selection;
                }
                finalize_holdout(entry, settings);
            }

            if (!entry.stable)
            {
                prepare_learning_state(entry, settings);
                if (entry.phase == Phase::Holdout)
                {
                    const std::size_t holdout_index = next_holdout_index(entry, settings);
                    if (holdout_index != invalid_index())
                    {
                        begin_probe(entry, selection, holdout_index, false, true, false);
                        return selection;
                    }
                }
                const std::size_t learning_index = next_learning_index(entry);
                if (learning_index != invalid_index())
                {
                    begin_probe(entry, selection, learning_index, false, false, false);
                    return selection;
                }
                // Defensive fallback: bounded learning must always converge.
                choose_stable_from_training(entry, settings, false);
            }
        }

        ensure_training_baseline(entry);
        selection.route = entry.stable_route;
        selection.stable = true;

        if (entry.revalidation_phase != RevalidationPhase::Idle)
        {
            begin_next_revalidation_probe(entry, selection);
            return selection;
        }

        if (settings.force_stable_sample)
        {
            const std::size_t stable_index = route_index(entry, entry.stable_route);
            begin_probe(entry, selection, stable_index, false, false, false, true);
            return selection;
        }

        const bool periodic_revalidation = entry.current_revalidate_interval > 0
            && ++entry.uses_since_revalidation >= entry.current_revalidate_interval;
        if (entry.candidates.size() > 1
            && (settings.force_revalidation || periodic_revalidation))
        {
            start_revalidation(entry, selection);
        }
        return selection;
    }

    void complete(const RouteSelection& selection,
                  double elapsed_ms,
                  const RouteSelectorSettings& settings) noexcept
    {
        if (!selection.enabled || !selection.probe || elapsed_ms < 0.0)
            return;
        Shard& shard = shards_[shard_index(selection.key)];
        std::lock_guard<std::mutex> lock(shard.mutex);
        const auto found = shard.entries.find(selection.key);
        if (found == shard.entries.end())
            return;

        Entry& entry = found->second;
        entry.last_access = ++shard.access_epoch;
        const std::size_t index = route_index(entry, selection.route);
        if (index == invalid_index())
            return;

        if (selection.revalidation)
        {
            Measurement& target = selection.revalidation_stable_sample
                ? entry.revalidation_stable_samples
                : entry.revalidation_challenger_samples;
            add_sample(target, elapsed_ms, 2);
            release_probe(entry, selection);
            advance_revalidation(entry, selection, settings);
            return;
        }

        if (selection.drift_sample)
        {
            add_sample(entry.current_measurements[index], elapsed_ms, settings.sample_window);
            release_probe(entry, selection);
            process_drift_sample(entry, index, elapsed_ms, settings);
            return;
        }

        if (selection.warmup)
            ++entry.measurements[index].warmups;
        else if (selection.holdout)
            add_sample(entry.holdouts[index], elapsed_ms, settings.sample_window);
        else
            add_sample(entry.measurements[index], elapsed_ms, settings.sample_window);

        release_probe(entry, selection);

        if (selection.holdout && holdout_complete(entry, settings))
            finalize_holdout(entry, settings);
        else if (!selection.warmup && !selection.holdout)
            prepare_learning_state(entry, settings);
    }

    void cancel(const RouteSelection& selection) noexcept
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

    void clear() noexcept
    {
        for (Shard& shard : shards_)
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            shard.entries.clear();
            shard.access_epoch = 0;
        }
    }

    bool snapshot(const RouteProfileKey& key, RouteMeasurementSnapshot& output) const
    {
        const Shard& shard = shards_[shard_index(key)];
        std::lock_guard<std::mutex> lock(shard.mutex);
        const auto found = shard.entries.find(key);
        if (found == shard.entries.end())
            return false;
        const Entry& entry = found->second;
        output = {};
        output.stable_route = entry.stable_route;
        output.provisional_route = entry.candidates.empty()
            ? ExecutionRoute::NativeSequential
            : entry.candidates[entry.provisional_index];
        output.stable = entry.stable;
        output.holdout_active = entry.phase == Phase::Holdout;
        output.revalidation_active = entry.revalidation_phase != RevalidationPhase::Idle;
        output.drift_detected = entry.drift_detected;
        output.use_count = entry.uses_since_revalidation;
        output.revalidate_after_uses = entry.current_revalidate_interval;
        output.verification_failures = entry.verification_failures;
        output.route_switch_count = entry.route_switch_count;
        output.drift_strikes = entry.drift_strikes;
        output.revalidation_challenger = entry.candidates.empty()
            ? ExecutionRoute::NativeSequential
            : entry.candidates[entry.revalidation_challenger_index];
        output.training_baseline_ms = entry.training_baseline_ms;
        const std::size_t stable_index = entry.stable
            ? route_index(entry, entry.stable_route) : invalid_index();
        output.current_baseline_ms = stable_index != invalid_index()
            ? median(entry.current_measurements[stable_index]) : 0.0;
        output.last_revalidation_stable_ms = entry.last_revalidation_stable_ms;
        output.last_revalidation_challenger_ms = entry.last_revalidation_challenger_ms;
        output.routes = entry.candidates;
        output.active = entry.active;
        for (std::size_t index = 0; index < entry.measurements.size(); ++index)
        {
            const Measurement& measurement = entry.measurements[index];
            output.elapsed_ms.push_back(median(measurement));
            output.mad_ms.push_back(mad(measurement));
            output.minimum_ms.push_back(minimum(measurement));
            output.maximum_ms.push_back(maximum(measurement));
            output.current_elapsed_ms.push_back(median(entry.current_measurements[index]));
            output.samples.push_back(measurement.sample_count);
            output.current_samples.push_back(entry.current_measurements[index].sample_count);
            output.warmups.push_back(measurement.warmups);
            output.holdout_samples.push_back(entry.holdouts[index].sample_count);
        }
        return true;
    }

  private:
    static constexpr std::size_t maximum_sample_window = 15;
    enum class Phase { Learning, Holdout, Stable };
    enum class RevalidationPhase { Idle, StableA, ChallengerA, ChallengerB, StableB };

    struct Measurement
    {
        std::size_t warmups = 0;
        std::array<double, maximum_sample_window> samples{};
        std::size_t sample_count = 0;
        std::size_t next_sample = 0;
    };

    struct Entry
    {
        std::vector<ExecutionRoute> candidates;
        std::vector<Measurement> measurements;
        std::vector<Measurement> holdouts;
        std::vector<Measurement> current_measurements;
        std::vector<bool> active;
        ExecutionRoute stable_route = ExecutionRoute::NativeSequential;
        Phase phase = Phase::Learning;
        bool stable = false;
        bool probe_in_progress = false;
        std::uint64_t active_probe_nonce = 0;
        std::size_t provisional_index = 0;
        std::size_t runner_up_index = 0;
        std::size_t learning_target_samples = 1;
        std::size_t verification_failures = 0;
        std::size_t uses_since_revalidation = 0;
        std::size_t current_revalidate_interval = 0;
        std::size_t next_revalidation_index = 0;
        RevalidationPhase revalidation_phase = RevalidationPhase::Idle;
        std::size_t revalidation_stable_index = 0;
        std::size_t revalidation_challenger_index = 0;
        Measurement revalidation_stable_samples;
        Measurement revalidation_challenger_samples;
        std::size_t drift_strikes = 0;
        bool drift_detected = false;
        std::size_t route_switch_count = 0;
        double training_baseline_ms = 0.0;
        double last_revalidation_stable_ms = 0.0;
        double last_revalidation_challenger_ms = 0.0;
        std::size_t rotation_seed = 0;
        std::uint64_t last_access = 0;
    };

    struct Shard
    {
        mutable std::mutex mutex;
        std::unordered_map<RouteProfileKey, Entry, RouteProfileKeyHasher> entries;
        std::uint64_t access_epoch = 0;
    };

    static constexpr std::size_t shard_count = 16;
    std::array<Shard, shard_count> shards_{};
    std::uint64_t next_probe_nonce_ = 0;
    mutable std::mutex nonce_mutex_;

    static std::size_t invalid_index() noexcept
    {
        return std::numeric_limits<std::size_t>::max();
    }

    static std::size_t normalized_maximum(std::size_t maximum_entries) noexcept
    {
        return maximum_entries == 0 ? 512 : maximum_entries;
    }

    static std::size_t normalized_minimum(const RouteSelectorSettings& settings) noexcept
    {
        return std::max<std::size_t>(1,
            std::min(settings.minimum_samples,
                     std::max<std::size_t>(1,
                         std::min(settings.sample_window, maximum_sample_window))));
    }

    static std::size_t normalized_window(const RouteSelectorSettings& settings) noexcept
    {
        return std::max<std::size_t>(normalized_minimum(settings),
            std::min(settings.sample_window == 0 ? normalized_minimum(settings)
                                                 : settings.sample_window,
                     maximum_sample_window));
    }

    static std::size_t shard_index(const RouteProfileKey& key) noexcept
    {
        return RouteProfileKeyHasher{}(key) % shard_count;
    }

    static std::size_t route_index(const Entry& entry, ExecutionRoute route) noexcept
    {
        const auto found = std::find(entry.candidates.begin(), entry.candidates.end(), route);
        return found == entry.candidates.end() ? invalid_index()
            : static_cast<std::size_t>(found - entry.candidates.begin());
    }

    static int route_preference(ExecutionRoute route) noexcept
    {
        switch (route)
        {
            case ExecutionRoute::NativeSequential: return 0;
            case ExecutionRoute::NativeThreadPool: return 1;
            case ExecutionRoute::NativeOneTbb: return 2;
            case ExecutionRoute::NativeStaticThread: return 3;
            case ExecutionRoute::OpenCV: return 4;
            case ExecutionRoute::Auto: return 5;
        }
        return 5;
    }

    static double median(const Measurement& measurement) noexcept
    {
        if (measurement.sample_count == 0)
            return 0.0;
        std::array<double, maximum_sample_window> sorted = measurement.samples;
        std::sort(sorted.begin(), sorted.begin() + measurement.sample_count);
        return sorted[measurement.sample_count / 2];
    }

    static double mad(const Measurement& measurement) noexcept
    {
        if (measurement.sample_count == 0)
            return 0.0;
        const double center = median(measurement);
        std::array<double, maximum_sample_window> deviations{};
        for (std::size_t index = 0; index < measurement.sample_count; ++index)
            deviations[index] = std::abs(measurement.samples[index] - center);
        std::sort(deviations.begin(), deviations.begin() + measurement.sample_count);
        return deviations[measurement.sample_count / 2];
    }

    static double minimum(const Measurement& measurement) noexcept
    {
        if (measurement.sample_count == 0)
            return 0.0;
        return *std::min_element(measurement.samples.begin(),
                                 measurement.samples.begin() + measurement.sample_count);
    }

    static double maximum(const Measurement& measurement) noexcept
    {
        if (measurement.sample_count == 0)
            return 0.0;
        return *std::max_element(measurement.samples.begin(),
                                 measurement.samples.begin() + measurement.sample_count);
    }

    static void add_sample(Measurement& measurement,
                           double elapsed_ms,
                           std::size_t requested_window) noexcept
    {
        const std::size_t window = std::max<std::size_t>(
            1, std::min(requested_window == 0 ? maximum_sample_window : requested_window,
                        maximum_sample_window));
        if (measurement.sample_count < window)
        {
            measurement.samples[measurement.sample_count++] = elapsed_ms;
            measurement.next_sample = measurement.sample_count % window;
        }
        else
        {
            measurement.samples[measurement.next_sample] = elapsed_ms;
            measurement.next_sample = (measurement.next_sample + 1) % window;
        }
    }

    static void reset_to_single_sample(Measurement& measurement,
                                       double elapsed_ms) noexcept
    {
        const std::size_t warmups = measurement.warmups;
        measurement = {};
        measurement.warmups = warmups;
        measurement.samples[0] = elapsed_ms;
        measurement.sample_count = 1;
        measurement.next_sample = 1;
    }

    static void clear_holdouts(Entry& entry) noexcept
    {
        for (Measurement& measurement : entry.holdouts)
            measurement = {};
    }

    static void reset_entry(Entry& entry,
                            const std::vector<ExecutionRoute>& candidates,
                            const RouteProfileKey& key,
                            const RouteSelectorSettings& settings)
    {
        const std::uint64_t last_access = entry.last_access;
        entry = Entry{};
        entry.candidates = candidates;
        entry.measurements.resize(candidates.size());
        entry.holdouts.resize(candidates.size());
        entry.current_measurements.resize(candidates.size());
        entry.active.assign(candidates.size(), true);
        entry.rotation_seed = candidates.empty() ? 0
            : RouteProfileKeyHasher{}(key) % candidates.size();
        entry.learning_target_samples = normalized_minimum(settings);
        entry.current_revalidate_interval = normalized_initial_interval(settings);
        entry.last_access = last_access;
    }

    static std::size_t normalized_initial_interval(
        const RouteSelectorSettings& settings) noexcept
    {
        if (settings.revalidate_interval == 0)
            return 0;
        const std::size_t initial = settings.initial_revalidate_interval == 0
            ? settings.revalidate_interval : settings.initial_revalidate_interval;
        return std::max<std::size_t>(1, std::min(initial, settings.revalidate_interval));
    }

    std::uint64_t next_nonce() noexcept
    {
        std::lock_guard<std::mutex> lock(nonce_mutex_);
        return ++next_probe_nonce_;
    }

    void begin_probe(Entry& entry,
                     RouteSelection& selection,
                     std::size_t index,
                     bool warmup,
                     bool holdout,
                     bool revalidation,
                     bool drift_sample = false,
                     bool revalidation_stable_sample = false,
                     std::uint8_t revalidation_stage = 0)
    {
        entry.probe_in_progress = true;
        entry.active_probe_nonce = next_nonce();
        selection.route = entry.candidates[index];
        selection.probe = true;
        selection.warmup = warmup;
        selection.holdout = holdout;
        selection.revalidation = revalidation;
        selection.drift_sample = drift_sample;
        selection.revalidation_stable_sample = revalidation_stable_sample;
        selection.revalidation_stage = revalidation_stage;
        selection.probe_nonce = entry.active_probe_nonce;
        selection.stable = entry.stable;
    }

    static void release_probe(Entry& entry, const RouteSelection& selection) noexcept
    {
        if (entry.probe_in_progress && entry.active_probe_nonce == selection.probe_nonce)
        {
            entry.probe_in_progress = false;
            entry.active_probe_nonce = 0;
        }
    }

    static void ensure_training_baseline(Entry& entry) noexcept
    {
        if (!entry.stable || entry.training_baseline_ms > 0.0)
            return;
        const std::size_t index = route_index(entry, entry.stable_route);
        if (index != invalid_index())
            entry.training_baseline_ms = median(entry.measurements[index]);
    }

    static double two_sample_center(const Measurement& measurement) noexcept
    {
        if (measurement.sample_count == 0)
            return 0.0;
        if (measurement.sample_count == 1)
            return measurement.samples[0];
        return 0.5 * (measurement.samples[0] + measurement.samples[1]);
    }

    static void initialize_revalidation(Entry& entry) noexcept
    {
        entry.revalidation_stable_samples = {};
        entry.revalidation_challenger_samples = {};
        entry.revalidation_stable_index = route_index(entry, entry.stable_route);
        entry.revalidation_challenger_index = next_alternative(
            entry, entry.revalidation_stable_index);
        entry.uses_since_revalidation = 0;
    }

    void start_revalidation(Entry& entry, RouteSelection& selection)
    {
        initialize_revalidation(entry);
        entry.revalidation_phase = RevalidationPhase::StableA;
        begin_next_revalidation_probe(entry, selection);
    }

    void begin_next_revalidation_probe(Entry& entry, RouteSelection& selection)
    {
        switch (entry.revalidation_phase)
        {
            case RevalidationPhase::StableA:
                begin_probe(entry, selection, entry.revalidation_stable_index,
                            false, false, true, false, true, 1);
                break;
            case RevalidationPhase::ChallengerA:
                begin_probe(entry, selection, entry.revalidation_challenger_index,
                            false, false, true, false, false, 2);
                break;
            case RevalidationPhase::ChallengerB:
                begin_probe(entry, selection, entry.revalidation_challenger_index,
                            false, false, true, false, false, 3);
                break;
            case RevalidationPhase::StableB:
                begin_probe(entry, selection, entry.revalidation_stable_index,
                            false, false, true, false, true, 4);
                break;
            case RevalidationPhase::Idle:
                break;
        }
    }

    static void finalize_revalidation(Entry& entry,
                                      const RouteSelectorSettings& settings) noexcept
    {
        const double stable_ms = two_sample_center(entry.revalidation_stable_samples);
        const double challenger_ms = two_sample_center(entry.revalidation_challenger_samples);
        entry.last_revalidation_stable_ms = stable_ms;
        entry.last_revalidation_challenger_ms = challenger_ms;

        for (std::size_t index = 0; index < entry.revalidation_stable_samples.sample_count; ++index)
            add_sample(entry.current_measurements[entry.revalidation_stable_index],
                       entry.revalidation_stable_samples.samples[index],
                       settings.sample_window);
        for (std::size_t index = 0; index < entry.revalidation_challenger_samples.sample_count; ++index)
            add_sample(entry.current_measurements[entry.revalidation_challenger_index],
                       entry.revalidation_challenger_samples.samples[index],
                       settings.sample_window);

        const double margin = std::max(
            std::max(0.0, settings.absolute_equivalence_ms),
            std::min(stable_ms, challenger_ms)
                * (std::max(1.0, settings.equivalence_ratio) - 1.0));
        const bool challenger_faster = challenger_ms + margin < stable_ms;
        const bool equivalent = std::abs(challenger_ms - stable_ms) <= margin;
        const bool preferred_equivalent = equivalent
            && route_preference(entry.candidates[entry.revalidation_challenger_index])
                < route_preference(entry.candidates[entry.revalidation_stable_index]);
        if (challenger_faster || preferred_equivalent)
        {
            entry.stable_route = entry.candidates[entry.revalidation_challenger_index];
            entry.training_baseline_ms = median(
                entry.measurements[entry.revalidation_challenger_index]);
            ++entry.route_switch_count;
            entry.current_revalidate_interval = normalized_initial_interval(settings);
        }
        else
        {
            const std::size_t maximum_interval = settings.revalidate_interval;
            if (maximum_interval > 0)
            {
                const std::size_t current = std::max<std::size_t>(
                    1, entry.current_revalidate_interval);
                entry.current_revalidate_interval = std::min(
                    maximum_interval, current > maximum_interval / 2
                        ? maximum_interval : current * 2);
            }
        }

        entry.revalidation_phase = RevalidationPhase::Idle;
        entry.revalidation_stable_samples = {};
        entry.revalidation_challenger_samples = {};
        entry.drift_strikes = 0;
        entry.uses_since_revalidation = 0;
    }

    static void advance_revalidation(Entry& entry,
                                     const RouteSelection& selection,
                                     const RouteSelectorSettings& settings) noexcept
    {
        switch (selection.revalidation_stage)
        {
            case 1: entry.revalidation_phase = RevalidationPhase::ChallengerA; break;
            case 2: entry.revalidation_phase = RevalidationPhase::ChallengerB; break;
            case 3: entry.revalidation_phase = RevalidationPhase::StableB; break;
            case 4: finalize_revalidation(entry, settings); break;
            default: break;
        }
    }

    static void process_drift_sample(Entry& entry,
                                     std::size_t stable_index,
                                     double elapsed_ms,
                                     const RouteSelectorSettings& settings) noexcept
    {
        const double baseline = entry.last_revalidation_stable_ms > 0.0
            ? entry.last_revalidation_stable_ms : entry.training_baseline_ms;
        const double drift_limit = baseline > 0.0
            ? std::max(baseline * std::max(1.0, settings.drift_ratio),
                       baseline + std::max(0.0, settings.drift_absolute_ms))
            : std::numeric_limits<double>::infinity();
        if (elapsed_ms > drift_limit)
            ++entry.drift_strikes;
        else
            entry.drift_strikes = 0;

        const std::size_t required = std::max<std::size_t>(1, settings.drift_required_samples);
        if (entry.drift_strikes < required || entry.candidates.size() < 2)
            return;

        entry.drift_detected = true;
        initialize_revalidation(entry);
        entry.revalidation_stable_index = stable_index;
        add_sample(entry.revalidation_stable_samples, elapsed_ms, 2);
        entry.revalidation_phase = RevalidationPhase::ChallengerA;
    }

    static std::size_t next_warmup_index(
        const Entry& entry,
        const RouteSelectorSettings& settings) noexcept
    {
        const std::size_t required = settings.warmup_samples;
        if (required == 0 || entry.candidates.empty())
            return invalid_index();
        std::size_t minimum_count = required;
        for (const Measurement& measurement : entry.measurements)
            minimum_count = std::min(minimum_count, measurement.warmups);
        if (minimum_count >= required)
            return invalid_index();
        const std::size_t count = entry.candidates.size();
        const std::size_t start = (entry.rotation_seed + minimum_count) % count;
        for (std::size_t offset = 0; offset < count; ++offset)
        {
            const std::size_t index = (start + offset) % count;
            if (entry.measurements[index].warmups == minimum_count)
                return index;
        }
        return invalid_index();
    }

    static bool active_round_complete(const Entry& entry) noexcept
    {
        for (std::size_t index = 0; index < entry.active.size(); ++index)
        {
            if (entry.active[index]
                && entry.measurements[index].sample_count < entry.learning_target_samples)
                return false;
        }
        return true;
    }

    static std::size_t next_learning_index(const Entry& entry) noexcept
    {
        if (entry.phase != Phase::Learning || entry.candidates.empty())
            return invalid_index();
        std::size_t minimum_count = std::numeric_limits<std::size_t>::max();
        for (std::size_t index = 0; index < entry.active.size(); ++index)
            if (entry.active[index])
                minimum_count = std::min(minimum_count,
                                         entry.measurements[index].sample_count);
        if (minimum_count == std::numeric_limits<std::size_t>::max()
            || minimum_count >= entry.learning_target_samples)
            return invalid_index();
        const std::size_t count = entry.candidates.size();
        const std::size_t start = (entry.rotation_seed + minimum_count) % count;
        for (std::size_t offset = 0; offset < count; ++offset)
        {
            const std::size_t index = (start + offset) % count;
            if (entry.active[index]
                && entry.measurements[index].sample_count == minimum_count)
                return index;
        }
        return invalid_index();
    }

    static double equivalence_margin(double fastest_ms,
                                     const RouteSelectorSettings& settings) noexcept
    {
        return std::max(std::max(0.0, settings.absolute_equivalence_ms),
                        fastest_ms * (std::max(1.0, settings.equivalence_ratio) - 1.0));
    }

    static double uncertainty_radius(const Measurement& measurement,
                                     const RouteSelectorSettings& settings) noexcept
    {
        return std::max(std::max(0.0, settings.absolute_equivalence_ms),
                        2.0 * mad(measurement));
    }

    static std::size_t fastest_index(const Entry& entry,
                                     const std::vector<bool>& eligible) noexcept
    {
        std::size_t fastest = invalid_index();
        for (std::size_t index = 0; index < entry.measurements.size(); ++index)
        {
            if (!eligible[index] || entry.measurements[index].sample_count == 0)
                continue;
            if (fastest == invalid_index()
                || median(entry.measurements[index]) < median(entry.measurements[fastest]))
                fastest = index;
        }
        return fastest;
    }

    static std::size_t preferred_equivalent_index(
        const Entry& entry,
        const std::vector<bool>& eligible,
        std::size_t fastest,
        const RouteSelectorSettings& settings) noexcept
    {
        const double fastest_ms = median(entry.measurements[fastest]);
        const double margin = equivalence_margin(fastest_ms, settings);
        std::size_t preferred = fastest;
        for (std::size_t index = 0; index < entry.measurements.size(); ++index)
        {
            if (!eligible[index])
                continue;
            const double allowance = margin + std::max(
                uncertainty_radius(entry.measurements[fastest], settings),
                uncertainty_radius(entry.measurements[index], settings));
            if (median(entry.measurements[index]) <= fastest_ms + allowance
                && route_preference(entry.candidates[index])
                    < route_preference(entry.candidates[preferred]))
                preferred = index;
        }
        return preferred;
    }

    static std::size_t runner_up_index(const Entry& entry,
                                       std::size_t provisional) noexcept
    {
        std::size_t runner = invalid_index();
        for (std::size_t index = 0; index < entry.measurements.size(); ++index)
        {
            if (index == provisional || entry.measurements[index].sample_count == 0)
                continue;
            if (runner == invalid_index()
                || median(entry.measurements[index]) < median(entry.measurements[runner]))
                runner = index;
        }
        return runner;
    }

    static void start_holdout_or_stabilize(Entry& entry,
                                           const RouteSelectorSettings& settings) noexcept
    {
        const std::size_t fastest = fastest_index(entry, entry.active);
        if (fastest == invalid_index())
            return;
        entry.provisional_index = preferred_equivalent_index(
            entry, entry.active, fastest, settings);
        entry.runner_up_index = runner_up_index(entry, entry.provisional_index);
        clear_holdouts(entry);
        if (entry.runner_up_index == invalid_index() || settings.holdout_samples == 0)
        {
            entry.stable_route = entry.candidates[entry.provisional_index];
            entry.stable = true;
            entry.phase = Phase::Stable;
            entry.uses_since_revalidation = 0;
            entry.current_revalidate_interval = normalized_initial_interval(settings);
        }
        else
        {
            entry.phase = Phase::Holdout;
        }
    }

    static void prepare_learning_state(Entry& entry,
                                       const RouteSelectorSettings& settings) noexcept
    {
        if (entry.stable || entry.phase != Phase::Learning || !active_round_complete(entry))
            return;

        const std::size_t fastest = fastest_index(entry, entry.active);
        if (fastest == invalid_index())
            return;
        const double fastest_ms = median(entry.measurements[fastest]);
        const double fastest_upper = fastest_ms
            + uncertainty_radius(entry.measurements[fastest], settings);
        const double margin = equivalence_margin(fastest_ms, settings);
        bool uncertain = false;
        std::size_t active_count = 0;
        for (std::size_t index = 0; index < entry.active.size(); ++index)
        {
            if (!entry.active[index])
                continue;
            if (index != fastest)
            {
                const double candidate_ms = median(entry.measurements[index]);
                const double candidate_lower = std::max(
                    0.0, candidate_ms - uncertainty_radius(entry.measurements[index], settings));
                if (candidate_lower > fastest_upper + margin)
                    entry.active[index] = false;
                else if (candidate_ms > fastest_ms + margin)
                    uncertain = true;
            }
            if (entry.active[index])
                ++active_count;
        }

        const std::size_t maximum_samples = normalized_window(settings);
        if (uncertain && active_count > 1
            && entry.learning_target_samples < maximum_samples)
        {
            entry.learning_target_samples = std::min(
                maximum_samples, entry.learning_target_samples + 2);
            return;
        }
        start_holdout_or_stabilize(entry, settings);
    }

    static std::size_t next_holdout_index(
        const Entry& entry,
        const RouteSelectorSettings& settings) noexcept
    {
        if (entry.phase != Phase::Holdout || settings.holdout_samples == 0)
            return invalid_index();
        const std::size_t first = (entry.verification_failures % 2u) == 0u
            ? entry.provisional_index : entry.runner_up_index;
        const std::size_t second = first == entry.provisional_index
            ? entry.runner_up_index : entry.provisional_index;
        const std::size_t first_count = entry.holdouts[first].sample_count;
        const std::size_t second_count = entry.holdouts[second].sample_count;
        if (first_count < settings.holdout_samples
            && first_count <= second_count)
            return first;
        if (second_count < settings.holdout_samples)
            return second;
        if (first_count < settings.holdout_samples)
            return first;
        return invalid_index();
    }

    static bool holdout_complete(const Entry& entry,
                                 const RouteSelectorSettings& settings) noexcept
    {
        return entry.phase == Phase::Holdout
            && entry.holdouts[entry.provisional_index].sample_count
                >= settings.holdout_samples
            && entry.holdouts[entry.runner_up_index].sample_count
                >= settings.holdout_samples;
    }

    static void merge_holdout(Measurement& target,
                              const Measurement& source,
                              std::size_t window) noexcept
    {
        for (std::size_t index = 0; index < source.sample_count; ++index)
            add_sample(target, source.samples[index], window);
    }

    static void finalize_holdout(Entry& entry,
                                 const RouteSelectorSettings& settings) noexcept
    {
        if (!holdout_complete(entry, settings))
            return;
        const Measurement& provisional = entry.holdouts[entry.provisional_index];
        const Measurement& runner = entry.holdouts[entry.runner_up_index];
        const double provisional_ms = median(provisional);
        const double runner_ms = median(runner);
        const double allowance = equivalence_margin(runner_ms, settings)
            + std::max(uncertainty_radius(provisional, settings),
                       uncertainty_radius(runner, settings));
        if (provisional_ms <= runner_ms + allowance)
        {
            entry.stable_route = entry.candidates[entry.provisional_index];
            entry.stable = true;
            entry.phase = Phase::Stable;
            entry.uses_since_revalidation = 0;
            entry.current_revalidate_interval = normalized_initial_interval(settings);
            return;
        }

        ++entry.verification_failures;
        merge_holdout(entry.measurements[entry.provisional_index], provisional,
                      normalized_window(settings));
        merge_holdout(entry.measurements[entry.runner_up_index], runner,
                      normalized_window(settings));
        if (entry.verification_failures >= settings.maximum_verification_failures
            || std::min(entry.measurements[entry.provisional_index].sample_count,
                        entry.measurements[entry.runner_up_index].sample_count)
                >= normalized_window(settings))
        {
            const double margin = equivalence_margin(runner_ms, settings);
            std::size_t winner = runner_ms + margin < provisional_ms
                ? entry.runner_up_index : entry.provisional_index;
            if (std::abs(provisional_ms - runner_ms) <= margin
                && route_preference(entry.candidates[entry.provisional_index])
                   < route_preference(entry.candidates[entry.runner_up_index]))
                winner = entry.provisional_index;
            entry.stable_route = entry.candidates[winner];
            entry.stable = true;
            entry.phase = Phase::Stable;
            entry.uses_since_revalidation = 0;
            entry.current_revalidate_interval = normalized_initial_interval(settings);
            return;
        }

        entry.phase = Phase::Learning;
        entry.active.assign(entry.candidates.size(), false);
        entry.active[entry.provisional_index] = true;
        entry.active[entry.runner_up_index] = true;
        const std::size_t current = std::min(
            entry.measurements[entry.provisional_index].sample_count,
            entry.measurements[entry.runner_up_index].sample_count);
        entry.learning_target_samples = std::min(
            normalized_window(settings), std::max(normalized_minimum(settings), current + 2));
        clear_holdouts(entry);
    }

    static void choose_stable_from_training(Entry& entry,
                                            const RouteSelectorSettings& settings,
                                            bool preserve_revalidation_interval) noexcept
    {
        std::vector<bool> eligible(entry.candidates.size(), true);
        const std::size_t fastest = fastest_index(entry, eligible);
        const std::size_t preferred = preferred_equivalent_index(
            entry, eligible, fastest, settings);
        entry.stable_route = entry.candidates[preferred];
        entry.stable = true;
        entry.phase = Phase::Stable;
        entry.uses_since_revalidation = 0;
        if (!preserve_revalidation_interval)
            entry.current_revalidate_interval = normalized_initial_interval(settings);
    }

    static std::size_t next_alternative(Entry& entry, std::size_t stable_index) noexcept
    {
        for (std::size_t offset = 0; offset < entry.candidates.size(); ++offset)
        {
            const std::size_t index = entry.next_revalidation_index++ % entry.candidates.size();
            if (index != stable_index)
                return index;
        }
        return stable_index;
    }

    static void make_room_locked(Shard& shard, std::size_t maximum_entries)
    {
        const std::size_t per_shard = std::max<std::size_t>(1, maximum_entries / shard_count);
        if (shard.entries.size() < per_shard)
            return;
        auto victim = shard.entries.end();
        for (auto iterator = shard.entries.begin(); iterator != shard.entries.end(); ++iterator)
        {
            if (victim == shard.entries.end()
                || iterator->second.last_access < victim->second.last_access)
                victim = iterator;
        }
        if (victim != shard.entries.end())
            shard.entries.erase(victim);
    }
};
} // namespace smart::vision::detail
