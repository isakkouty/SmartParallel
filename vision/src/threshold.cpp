#include <smart/vision/threshold.hpp>

#include "opencv_provider.hpp"

#include <smart/core/config.hpp>
#include <smart/execution/backend.hpp>
#include <smart/execution/execution_context.hpp>
#include <smart/execution/runtime_capabilities.hpp>
#include <smart/hardware/hardware.hpp>
#include <smart/vision/detail/adaptive_route_selector.hpp>
#include <smart/vision/detail/threshold_kernel.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace smart::vision
{
namespace
{
constexpr std::uint64_t threshold_operation_id = 0x5631355448524553ull;

thread_local DecisionReport last_report_storage{};
thread_local RouteTrainingReport last_training_report_storage{};
detail::AdaptiveRouteSelector route_selector;
std::atomic<std::size_t> route_cache_epoch{1};
std::atomic<std::uint64_t> hot_route_epoch{1};

struct HotRouteKey
{
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t source_stride = 0;
    std::size_t destination_stride = 0;
    std::size_t parameter_hash = 0;
    std::size_t worker_budget = 1;
    std::size_t candidate_mask = 0;
    std::size_t policy_signature = 0;
    std::uint64_t provider_generation = 0;
    std::size_t source_alignment = 1;
    std::size_t destination_alignment = 1;
    bool in_place = false;

    bool operator==(const HotRouteKey& other) const noexcept
    {
        return width == other.width && height == other.height
            && source_stride == other.source_stride
            && destination_stride == other.destination_stride
            && parameter_hash == other.parameter_hash
            && worker_budget == other.worker_budget
            && candidate_mask == other.candidate_mask
            && policy_signature == other.policy_signature
            && provider_generation == other.provider_generation
            && source_alignment == other.source_alignment
            && destination_alignment == other.destination_alignment
            && in_place == other.in_place;
    }
};

struct HotRouteEntry
{
    bool valid = false;
    HotRouteKey hot_key{};
    detail::RouteProfileKey profile_key{};
    ExecutionRoute route = ExecutionRoute::NativeSequential;
    std::size_t uses = 0;
    std::size_t revalidate_after_uses = 0;
    std::size_t drift_uses = 0;
    std::size_t drift_after_uses = 0;
    std::uint64_t epoch = 0;
};

thread_local std::array<HotRouteEntry, 16> hot_route_entries{};

std::size_t bucket(std::size_t value) noexcept
{
    if (value == 0)
        return 0;
    std::size_t result = 1;
    while (result < value && result <= std::numeric_limits<std::size_t>::max() / 2)
        result <<= 1u;
    return result;
}

std::size_t combine_hash(std::size_t seed, std::size_t value) noexcept
{
    return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u));
}

std::size_t normalized_worker_budget(std::size_t requested) noexcept
{
    std::size_t budget = requested == 0 ? hardware_threads() : requested;
    if (global_config().nested_root_concurrency_budget != 0)
        budget = std::min(budget, global_config().nested_root_concurrency_budget);
    return std::max<std::size_t>(1, budget);
}

bool valid_threshold_mode(ThresholdMode mode) noexcept
{
    return mode == ThresholdMode::Binary || mode == ThresholdMode::BinaryInverse;
}

bool valid_execution_route(ExecutionRoute route) noexcept
{
    switch (route)
    {
        case ExecutionRoute::Auto:
        case ExecutionRoute::NativeSequential:
        case ExecutionRoute::NativeThreadPool:
        case ExecutionRoute::NativeStaticThread:
        case ExecutionRoute::NativeOneTbb:
        case ExecutionRoute::OpenCV:
            return true;
    }
    return false;
}

template <typename T>
std::size_t image_span_bytes(ImageView<T> image)
{
    if (image.empty())
        return 0;
    const std::size_t row_bytes = image.row_bytes();
    if (image.height > 1
        && image.stride_bytes
            > (std::numeric_limits<std::size_t>::max() - row_bytes)
                / (image.height - 1))
    {
        throw std::length_error("smart::vision::threshold image extent overflows size_t");
    }
    return (image.height - 1) * image.stride_bytes + row_bytes;
}

void validate(ImageView<const std::uint8_t> source,
              ImageView<std::uint8_t> destination,
              ThresholdOptions options,
              ExecutionPolicy policy)
{
    if (!valid_threshold_mode(options.mode))
        throw std::invalid_argument("smart::vision::threshold received an invalid threshold mode");
    if (!valid_execution_route(policy.route))
        throw std::invalid_argument("smart::vision::threshold received an invalid execution route");
    if (source.width != destination.width || source.height != destination.height)
        throw std::invalid_argument("smart::vision::threshold requires matching dimensions");
    if (source.channels != 1 || destination.channels != 1)
        throw std::invalid_argument("smart::vision::threshold currently requires one channel");
    if (source.empty())
        return;
    if (source.data == nullptr || destination.data == nullptr)
        throw std::invalid_argument("smart::vision::threshold received a null image buffer");
    if (source.stride_bytes < source.row_bytes()
        || destination.stride_bytes < destination.row_bytes())
    {
        throw std::invalid_argument("smart::vision::threshold received an invalid row stride");
    }

    const std::size_t source_span = image_span_bytes(source);
    const std::size_t destination_span = image_span_bytes(destination);
    const std::uintptr_t source_begin = reinterpret_cast<std::uintptr_t>(source.data);
    const std::uintptr_t destination_begin = reinterpret_cast<std::uintptr_t>(destination.data);
    if (source_span > std::numeric_limits<std::uintptr_t>::max() - source_begin
        || destination_span > std::numeric_limits<std::uintptr_t>::max() - destination_begin)
    {
        throw std::length_error("smart::vision::threshold image address range overflows");
    }
    const std::uintptr_t source_end = source_begin + source_span;
    const std::uintptr_t destination_end = destination_begin + destination_span;
    if (source_begin == destination_begin)
    {
        if (source.stride_bytes != destination.stride_bytes)
            throw std::invalid_argument("in-place threshold requires matching row strides");
    }
    else if (source_begin < destination_end && destination_begin < source_end)
    {
        throw std::invalid_argument("smart::vision::threshold rejects partially overlapping buffers");
    }
}

std::size_t route_bit(ExecutionRoute route) noexcept
{
    return std::size_t{1} << static_cast<unsigned>(route);
}

bool opencv_compatible(ImageView<const std::uint8_t> source,
                       ImageView<std::uint8_t> destination) noexcept
{
    return source.width <= static_cast<std::size_t>(std::numeric_limits<int>::max())
        && source.height <= static_cast<std::size_t>(std::numeric_limits<int>::max())
        && destination.width <= static_cast<std::size_t>(std::numeric_limits<int>::max())
        && destination.height <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

std::size_t automatic_candidate_mask(ImageView<const std::uint8_t> source,
                                     ImageView<std::uint8_t> destination) noexcept
{
    const Config& config = global_config();
    std::size_t mask = route_bit(ExecutionRoute::NativeSequential);
    if (config.vision_route_consider_opencv && detail::opencv_provider_available()
        && opencv_compatible(source, destination))
    {
        mask |= route_bit(ExecutionRoute::OpenCV);
    }
    if (config.vision_route_consider_thread_pool)
        mask |= route_bit(ExecutionRoute::NativeThreadPool);
    if (config.vision_route_consider_one_tbb
        && execution_backend_available(ExecutionEngineType::OneTbb))
    {
        mask |= route_bit(ExecutionRoute::NativeOneTbb);
    }
    return mask;
}

std::vector<ExecutionRoute> automatic_candidates(std::size_t mask)
{
    std::vector<ExecutionRoute> candidates;
    candidates.reserve(4);
    for (ExecutionRoute route : {ExecutionRoute::NativeSequential,
                                 ExecutionRoute::OpenCV,
                                 ExecutionRoute::NativeThreadPool,
                                 ExecutionRoute::NativeOneTbb})
    {
        if ((mask & route_bit(route)) != 0)
            candidates.push_back(route);
    }
    return candidates;
}

std::size_t candidate_count(std::size_t mask) noexcept
{
    std::size_t count = 0;
    while (mask != 0)
    {
        count += mask & 1u;
        mask >>= 1u;
    }
    return count;
}

std::size_t vision_policy_signature(const Config& config) noexcept
{
    std::size_t hash = config.vision_route_policy_generation;
    hash = combine_hash(hash, config.enable_vision_adaptive_routes ? 1u : 0u);
    hash = combine_hash(hash, config.vision_route_cache_max_entries);
    hash = combine_hash(hash, std::hash<double>{}(config.vision_route_equivalence_ratio));
    hash = combine_hash(hash, std::hash<double>{}(config.vision_route_measurement_blend));
    hash = combine_hash(hash, std::hash<double>{}(config.vision_route_absolute_equivalence_ms));
    hash = combine_hash(hash, config.vision_route_warmup_samples);
    hash = combine_hash(hash, config.vision_route_minimum_samples);
    hash = combine_hash(hash, config.vision_route_sample_window);
    hash = combine_hash(hash, config.vision_route_holdout_samples);
    hash = combine_hash(hash, config.vision_route_maximum_verification_failures);
    hash = combine_hash(hash, config.vision_route_initial_revalidate_interval);
    hash = combine_hash(hash, config.vision_route_revalidate_interval);
    hash = combine_hash(hash, config.vision_route_drift_sample_interval);
    hash = combine_hash(hash, config.vision_route_drift_required_samples);
    hash = combine_hash(hash, std::hash<double>{}(config.vision_route_drift_ratio));
    hash = combine_hash(hash, std::hash<double>{}(config.vision_route_drift_absolute_ms));
    hash = combine_hash(hash, config.vision_route_consider_thread_pool ? 1u : 0u);
    hash = combine_hash(hash, config.vision_route_consider_one_tbb ? 1u : 0u);
    hash = combine_hash(hash, config.vision_route_consider_opencv ? 1u : 0u);
    hash = combine_hash(hash, route_cache_epoch.load(std::memory_order_acquire));
    return hash;
}

detail::RouteSelectorSettings selector_settings()
{
    const Config& config = global_config();
    detail::RouteSelectorSettings settings;
    settings.maximum_entries = config.vision_route_cache_max_entries;
    settings.equivalence_ratio = config.vision_route_equivalence_ratio;
    settings.measurement_blend = config.vision_route_measurement_blend;
    settings.absolute_equivalence_ms = config.vision_route_absolute_equivalence_ms;
    settings.warmup_samples = config.vision_route_warmup_samples;
    settings.minimum_samples = config.vision_route_minimum_samples;
    settings.sample_window = config.vision_route_sample_window;
    settings.holdout_samples = config.vision_route_holdout_samples;
    settings.maximum_verification_failures =
        config.vision_route_maximum_verification_failures;
    settings.initial_revalidate_interval =
        config.vision_route_initial_revalidate_interval;
    settings.revalidate_interval = config.vision_route_revalidate_interval;
    settings.drift_required_samples = config.vision_route_drift_required_samples;
    settings.drift_ratio = config.vision_route_drift_ratio;
    settings.drift_absolute_ms = config.vision_route_drift_absolute_ms;
    return settings;
}

std::size_t alignment_class(const void* pointer) noexcept
{
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(pointer);
    if ((address & 63u) == 0)
        return 64;
    if ((address & 31u) == 0)
        return 32;
    if ((address & 15u) == 0)
        return 16;
    if ((address & 7u) == 0)
        return 8;
    return 1;
}

std::size_t threshold_parameter_hash(ThresholdOptions options) noexcept
{
    std::size_t hash = static_cast<std::size_t>(options.threshold);
    hash = combine_hash(hash, options.maximum_value);
    return combine_hash(hash, static_cast<std::size_t>(options.mode));
}

HotRouteKey make_hot_key(ImageView<const std::uint8_t> source,
                         ImageView<std::uint8_t> destination,
                         ThresholdOptions options,
                         std::size_t worker_budget,
                         std::size_t candidates_mask,
                         std::size_t policy_signature,
                         std::uint64_t provider_generation) noexcept
{
    return {source.width,
            source.height,
            source.stride_bytes,
            destination.stride_bytes,
            threshold_parameter_hash(options),
            worker_budget,
            candidates_mask,
            policy_signature,
            provider_generation,
            alignment_class(source.data),
            alignment_class(destination.data),
            source.data == destination.data};
}

std::size_t hot_key_hash(const HotRouteKey& key) noexcept
{
    std::size_t hash = key.width;
    hash = combine_hash(hash, key.height);
    hash = combine_hash(hash, key.source_stride);
    hash = combine_hash(hash, key.destination_stride);
    hash = combine_hash(hash, key.parameter_hash);
    hash = combine_hash(hash, key.worker_budget);
    hash = combine_hash(hash, key.candidate_mask);
    hash = combine_hash(hash, key.policy_signature);
    hash = combine_hash(hash, static_cast<std::size_t>(key.provider_generation));
    hash = combine_hash(hash, key.source_alignment);
    hash = combine_hash(hash, key.destination_alignment);
    return combine_hash(hash, key.in_place ? 1u : 0u);
}

detail::RouteProfileKey make_profile_key(ImageView<const std::uint8_t> source,
                                         ImageView<std::uint8_t> destination,
                                         ThresholdOptions options,
                                         std::size_t worker_budget,
                                         std::size_t candidates_mask,
                                         std::size_t policy_signature,
                                         std::size_t provider_fingerprint)
{
    std::size_t layout = source.contiguous() && destination.contiguous() ? 1u : 2u;
    layout = combine_hash(layout, source.data == destination.data ? 1u : 0u);
    layout = combine_hash(layout, bucket(source.stride_bytes));
    layout = combine_hash(layout, bucket(destination.stride_bytes));
    layout = combine_hash(layout, alignment_class(source.data));
    layout = combine_hash(layout, alignment_class(destination.data));
    return detail::RouteProfileKey{
        threshold_operation_id,
        bucket(source.width * source.height),
        bucket(source.width),
        bucket(source.height),
        layout,
        threshold_parameter_hash(options),
        worker_budget,
        candidates_mask,
        policy_signature,
        provider_fingerprint};
}

HotRouteEntry* hot_route_lookup(const HotRouteKey& key) noexcept
{
    const std::size_t index = hot_key_hash(key) % hot_route_entries.size();
    HotRouteEntry& entry = hot_route_entries[index];
    const std::uint64_t epoch = hot_route_epoch.load(std::memory_order_acquire);
    return entry.valid && entry.hot_key == key && entry.epoch == epoch ? &entry : nullptr;
}

void hot_route_store(const HotRouteKey& hot_key,
                     const detail::RouteProfileKey& profile_key,
                     ExecutionRoute route,
                     std::size_t revalidate_after_uses,
                     std::size_t drift_after_uses,
                     std::size_t uses = 0,
                     std::size_t drift_uses = 0) noexcept
{
    const std::size_t index = hot_key_hash(hot_key) % hot_route_entries.size();
    hot_route_entries[index] = HotRouteEntry{
        true,
        hot_key,
        profile_key,
        route,
        uses,
        revalidate_after_uses,
        drift_uses,
        drift_after_uses,
        hot_route_epoch.load(std::memory_order_acquire)};
}

void invalidate_hot_routes() noexcept
{
    hot_route_epoch.fetch_add(1, std::memory_order_acq_rel);
}

void update_training_report(const detail::RouteMeasurementSnapshot& snapshot)
{
    last_training_report_storage = {};
    last_training_report_storage.available = true;
    last_training_report_storage.stable = snapshot.stable;
    last_training_report_storage.holdout_active = snapshot.holdout_active;
    last_training_report_storage.revalidation_active = snapshot.revalidation_active;
    last_training_report_storage.drift_detected = snapshot.drift_detected;
    last_training_report_storage.stable_route = snapshot.stable_route;
    last_training_report_storage.provisional_route = snapshot.provisional_route;
    last_training_report_storage.verification_failures = snapshot.verification_failures;
    last_training_report_storage.route_switch_count = snapshot.route_switch_count;
    last_training_report_storage.drift_strikes = snapshot.drift_strikes;
    last_training_report_storage.revalidation_challenger = snapshot.revalidation_challenger;
    last_training_report_storage.training_baseline_ms = snapshot.training_baseline_ms;
    last_training_report_storage.current_baseline_ms = snapshot.current_baseline_ms;
    last_training_report_storage.last_revalidation_stable_ms = snapshot.last_revalidation_stable_ms;
    last_training_report_storage.last_revalidation_challenger_ms = snapshot.last_revalidation_challenger_ms;
    last_training_report_storage.routes.reserve(snapshot.routes.size());
    for (std::size_t index = 0; index < snapshot.routes.size(); ++index)
    {
        RouteTrainingEntry entry;
        entry.route = snapshot.routes[index];
        entry.median_ms = snapshot.elapsed_ms[index];
        entry.mad_ms = snapshot.mad_ms[index];
        entry.minimum_ms = snapshot.minimum_ms[index];
        entry.maximum_ms = snapshot.maximum_ms[index];
        entry.sample_count = snapshot.samples[index];
        entry.warmup_count = snapshot.warmups[index];
        entry.holdout_sample_count = snapshot.holdout_samples[index];
        entry.current_sample_count = snapshot.current_samples[index];
        entry.current_median_ms = snapshot.current_elapsed_ms[index];
        entry.active = snapshot.active[index];
        last_training_report_storage.routes.push_back(entry);
    }
}


struct ChunkRange
{
    std::size_t begin = 0;
    std::size_t end = 0;
};

ChunkRange chunk_range(std::size_t total,
                       std::size_t chunks,
                       std::size_t ordinal) noexcept
{
    const std::size_t base = total / chunks;
    const std::size_t remainder = total % chunks;
    const std::size_t begin = ordinal * base + std::min(ordinal, remainder);
    return {begin, begin + base + (ordinal < remainder ? 1u : 0u)};
}

std::size_t native_chunk_count(ImageView<const std::uint8_t> source,
                               std::size_t worker_budget) noexcept
{
    const std::size_t units = source.contiguous()
        ? source.width * source.height
        : source.height;
    const std::size_t desired = worker_budget > std::numeric_limits<std::size_t>::max() / 4
        ? std::numeric_limits<std::size_t>::max()
        : worker_budget * 4;
    return std::max<std::size_t>(1, std::min(units, desired));
}

template <bool Inverse>
void execute_native_chunk_mode(ImageView<const std::uint8_t> source,
                               ImageView<std::uint8_t> destination,
                               ThresholdOptions options,
                               std::size_t chunks,
                               std::size_t ordinal) noexcept
{
    const bool in_place = source.data == destination.data;
    if (source.contiguous() && destination.contiguous())
    {
        const ChunkRange range = chunk_range(source.width * source.height, chunks, ordinal);
        if (in_place)
        {
            detail::threshold_u8_contiguous_in_place<Inverse>(
                destination.data + range.begin,
                range.end - range.begin,
                options.threshold,
                options.maximum_value);
        }
        else
        {
            detail::threshold_u8_contiguous_disjoint<Inverse>(
                source.data + range.begin,
                destination.data + range.begin,
                range.end - range.begin,
                options.threshold,
                options.maximum_value);
        }
        return;
    }

    const ChunkRange rows = chunk_range(source.height, chunks, ordinal);
    if (in_place)
    {
        detail::threshold_u8_rows_in_place<Inverse>(
            destination.data,
            destination.stride_bytes,
            source.width,
            rows.begin,
            rows.end,
            options.threshold,
            options.maximum_value);
    }
    else
    {
        detail::threshold_u8_rows_disjoint<Inverse>(
            source.data,
            source.stride_bytes,
            destination.data,
            destination.stride_bytes,
            source.width,
            rows.begin,
            rows.end,
            options.threshold,
            options.maximum_value);
    }
}

void execute_native_chunk(ImageView<const std::uint8_t> source,
                          ImageView<std::uint8_t> destination,
                          ThresholdOptions options,
                          std::size_t chunks,
                          std::size_t ordinal) noexcept
{
    if (options.mode == ThresholdMode::BinaryInverse)
        execute_native_chunk_mode<true>(source, destination, options, chunks, ordinal);
    else
        execute_native_chunk_mode<false>(source, destination, options, chunks, ordinal);
}

template <bool Inverse>
void execute_native_sequential_mode(ImageView<const std::uint8_t> source,
                                    ImageView<std::uint8_t> destination,
                                    ThresholdOptions options) noexcept
{
    const bool in_place = source.data == destination.data;
    if (source.contiguous() && destination.contiguous())
    {
        const std::size_t count = source.width * source.height;
        if (in_place)
        {
            detail::threshold_u8_contiguous_in_place<Inverse>(
                destination.data, count, options.threshold, options.maximum_value);
        }
        else
        {
            detail::threshold_u8_contiguous_disjoint<Inverse>(
                source.data,
                destination.data,
                count,
                options.threshold,
                options.maximum_value);
        }
        return;
    }

    if (in_place)
    {
        detail::threshold_u8_rows_in_place<Inverse>(
            destination.data,
            destination.stride_bytes,
            source.width,
            0,
            source.height,
            options.threshold,
            options.maximum_value);
    }
    else
    {
        detail::threshold_u8_rows_disjoint<Inverse>(
            source.data,
            source.stride_bytes,
            destination.data,
            destination.stride_bytes,
            source.width,
            0,
            source.height,
            options.threshold,
            options.maximum_value);
    }
}

struct NativeExecutionResult
{
    std::size_t chunks = 1;
    std::size_t participants = 1;
};

NativeExecutionResult execute_native(ExecutionRoute route,
                                     ImageView<const std::uint8_t> source,
                                     ImageView<std::uint8_t> destination,
                                     ThresholdOptions options,
                                     std::size_t worker_budget)
{
    if (source.empty())
        return {};
    if (route == ExecutionRoute::NativeSequential)
    {
        if (options.mode == ThresholdMode::BinaryInverse)
            execute_native_sequential_mode<true>(source, destination, options);
        else
            execute_native_sequential_mode<false>(source, destination, options);
        return {};
    }

    const ExecutionEngineType engine = native_execution_engine(route);
    if (!execution_backend_available(engine))
        throw std::runtime_error(std::string("Requested unavailable SmartParallel route: ")
                                 + execution_route_name(route));

    const std::size_t chunks = native_chunk_count(source, worker_budget);
    BackendExecutionRequest request;
    request.total = chunks;
    request.concurrency_budget = worker_budget;
    request.chunk_size = 1;
    const ExecutionContext context = current_execution_context();
    request.loop_id = context.loop_id;
    request.nested_session = context.nested_session;
    request.cooperative_helping = route == ExecutionRoute::NativeThreadPool;
    request.function = [=](std::size_t ordinal)
    {
        execute_native_chunk(source, destination, options, chunks, ordinal);
    };
    const BackendExecutionResult result = execution_backend(engine).execute(std::move(request));
    return {chunks, std::max<std::size_t>(1, result.runtime_concurrency)};
}

NativeExecutionResult execute_route(ExecutionRoute route,
                                    ImageView<const std::uint8_t> source,
                                    ImageView<std::uint8_t> destination,
                                    ThresholdOptions options,
                                    std::size_t worker_budget)
{
    if (route == ExecutionRoute::OpenCV)
    {
        if (!detail::opencv_provider_available())
            throw std::runtime_error("Requested OpenCV route is unavailable");
        if (!opencv_compatible(source, destination))
            throw std::invalid_argument("OpenCV threshold route requires dimensions representable as int");
        detail::execute_opencv_threshold(source, destination, options);
        return {1, 1};
    }
    return execute_native(route, source, destination, options, worker_budget);
}
} // namespace

void threshold(ImageView<const std::uint8_t> source,
               ImageView<std::uint8_t> destination,
               ThresholdOptions options,
               ExecutionPolicy policy)
{
    validate(source, destination, options, policy);
    last_report_storage = {};
    last_report_storage.requested_route = policy.route;
    last_report_storage.automatic = policy.route == ExecutionRoute::Auto;
    last_report_storage.opencv_available = detail::opencv_provider_available();
    const ExecutionContext context = current_execution_context();
    last_report_storage.execution_depth = context.depth;
    const std::size_t worker_budget = normalized_worker_budget(policy.worker_budget);
    last_report_storage.worker_budget = worker_budget;

    if (source.empty())
    {
        last_report_storage.selected_route = ExecutionRoute::NativeSequential;
        last_report_storage.native_engine = ExecutionEngineType::Auto;
        last_report_storage.backend_authenticated = true;
        return;
    }

    ExecutionRoute selected = policy.route;
    detail::RouteSelection selection;
    detail::RouteSelectorSettings settings;
    HotRouteKey active_hot_key{};
    bool have_hot_key = false;
    std::size_t preserved_hot_uses = 0;
    bool forced_drift_sample = false;
    bool forced_revalidation = false;

    if (policy.route == ExecutionRoute::Auto)
    {
        const Config& config = global_config();
        if (!config.enable_vision_adaptive_routes || context.depth != 0)
        {
            selected = ExecutionRoute::NativeSequential;
            last_report_storage.nested_fallback = context.depth != 0;
        }
        else
        {
            const std::size_t candidates_mask = automatic_candidate_mask(source, destination);
            const std::size_t policy_signature = vision_policy_signature(config);
            const detail::OpenCvProviderState provider_state =
                detail::opencv_provider_state();
            active_hot_key = make_hot_key(source,
                                          destination,
                                          options,
                                          worker_budget,
                                          candidates_mask,
                                          policy_signature,
                                          provider_state.generation);
            have_hot_key = true;
            last_report_storage.adaptive_selection_enabled = true;
            last_report_storage.candidate_count = candidate_count(candidates_mask);

            HotRouteEntry* hot_entry = hot_route_lookup(active_hot_key);
            bool hot_revalidation = false;
            bool hot_drift_sample = false;
            if (hot_entry != nullptr)
            {
                if (!config.vision_route_pause_maintenance)
                {
                    ++hot_entry->uses;
                    ++hot_entry->drift_uses;
                    hot_revalidation = hot_entry->revalidate_after_uses > 0
                        && hot_entry->uses >= hot_entry->revalidate_after_uses;
                    hot_drift_sample = !hot_revalidation
                        && hot_entry->drift_after_uses > 0
                        && hot_entry->drift_uses >= hot_entry->drift_after_uses;
                }
                preserved_hot_uses = hot_entry->uses;
            }
            if (hot_entry != nullptr && !hot_revalidation && !hot_drift_sample)
            {
                selected = hot_entry->route;
                last_report_storage.cache_hit = true;
                last_report_storage.learned_route = true;
            }
            else
            {
                detail::RouteProfileKey profile_key;
                if (hot_entry != nullptr)
                {
                    profile_key = hot_entry->profile_key;
                    hot_entry->valid = false;
                }
                else
                {
                    profile_key = make_profile_key(source,
                                                   destination,
                                                   options,
                                                   worker_budget,
                                                   candidates_mask,
                                                   policy_signature,
                                                   provider_state.fingerprint);
                }

                settings = selector_settings();
                const std::vector<ExecutionRoute> candidates =
                    automatic_candidates(candidates_mask);
                detail::RouteSelectorSettings selection_settings = settings;
                selection_settings.force_revalidation = hot_revalidation;
                selection_settings.force_stable_sample = hot_drift_sample;
                forced_revalidation = hot_revalidation;
                forced_drift_sample = hot_drift_sample;
                selection = route_selector.select(
                    profile_key, candidates, selection_settings);
                settings = selection_settings;
                selected = selection.route;
                last_report_storage.cache_hit = selection.cache_hit;
                last_report_storage.learned_route = selection.stable && !selection.probe;
                last_report_storage.holdout_probe = selection.holdout;
                last_report_storage.drift_probe = selection.drift_sample;
                last_report_storage.exploration_probe =
                    selection.probe && !selection.revalidation && !selection.drift_sample;
                last_report_storage.revalidation_probe = selection.revalidation;
                if (selection.stable && !selection.probe)
                {
                    detail::RouteMeasurementSnapshot snapshot;
                    const bool have_snapshot = route_selector.snapshot(profile_key, snapshot);
                    if (have_snapshot)
                        update_training_report(snapshot);
                    const std::size_t interval = have_snapshot
                        ? snapshot.revalidate_after_uses
                        : settings.initial_revalidate_interval;
                    hot_route_store(active_hot_key,
                                    profile_key,
                                    selected,
                                    interval,
                                    config.vision_route_drift_sample_interval);
                }
            }
        }
    }

    const bool stable_hot_route = policy.route == ExecutionRoute::Auto
        && last_report_storage.cache_hit && last_report_storage.learned_route
        && !last_report_storage.revalidation_probe
        && !last_report_storage.drift_probe;
    const bool measure_execution = !stable_hot_route;
    const auto start = measure_execution
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    NativeExecutionResult execution;
    try
    {
        execution = execute_route(selected, source, destination, options, worker_budget);
    }
    catch (...)
    {
        if (selection.enabled)
            route_selector.cancel(selection);
        throw;
    }
    const double elapsed_ms = measure_execution
        ? std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - start).count()
        : 0.0;

    if (selection.enabled)
    {
        detail::RouteMeasurementSnapshot before;
        const bool had_before = route_selector.snapshot(selection.key, before);
        route_selector.complete(selection, elapsed_ms, settings);
        detail::RouteMeasurementSnapshot snapshot;
        if (route_selector.snapshot(selection.key, snapshot))
        {
            update_training_report(snapshot);
            if (snapshot.stable)
            {
                if (!had_before || !before.stable
                    || before.stable_route != snapshot.stable_route)
                {
                    invalidate_hot_routes();
                }
                if (have_hot_key && !snapshot.revalidation_active)
                {
                    std::size_t hot_uses = 0;
                    std::size_t drift_uses = 0;
                    if (selection.drift_sample && !snapshot.drift_detected)
                    {
                        hot_uses = preserved_hot_uses;
                        drift_uses = 0;
                    }
                    else if (!selection.revalidation && forced_drift_sample
                             && !snapshot.revalidation_active)
                    {
                        hot_uses = preserved_hot_uses;
                    }
                    else if (!selection.revalidation && !forced_revalidation
                             && !selection.drift_sample)
                    {
                        hot_uses = 0;
                    }
                    hot_route_store(active_hot_key,
                                    selection.key,
                                    snapshot.stable_route,
                                    snapshot.revalidate_after_uses,
                                    global_config().vision_route_drift_sample_interval,
                                    hot_uses,
                                    drift_uses);
                }
            }
        }
    }

    last_report_storage.selected_route = selected;
    last_report_storage.native_engine = native_execution_engine(selected);
    last_report_storage.execution_ms = elapsed_ms;
    last_report_storage.execution_timed = measure_execution;
    last_report_storage.chunk_count = execution.chunks;
    last_report_storage.participant_count = execution.participants;
    last_report_storage.backend_authenticated = selected == ExecutionRoute::OpenCV
        ? detail::opencv_provider_available()
        : selected == ExecutionRoute::NativeSequential
            || execution_backend_available(native_execution_engine(selected));
}

bool opencv_available() noexcept
{
    return detail::opencv_provider_available();
}

std::string opencv_version()
{
    return detail::opencv_provider_version();
}

DecisionReport last_decision_report() noexcept
{
    return last_report_storage;
}

RouteTrainingReport last_route_training_report()
{
    return last_training_report_storage;
}

void refresh_provider_state() noexcept
{
    detail::refresh_opencv_provider_state();
    clear_adaptive_route_cache();
}

void clear_adaptive_route_cache() noexcept
{
    route_selector.clear();
    last_training_report_storage = {};
    route_cache_epoch.fetch_add(1, std::memory_order_acq_rel);
    invalidate_hot_routes();
    for (HotRouteEntry& entry : hot_route_entries)
        entry.valid = false;
}
} // namespace smart::vision
