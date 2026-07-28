#include <smart/execution/parallel.hpp>
#include <smart/vision/detail/adaptive_route_selector.hpp>
#include <smart/vision/detail/threshold_kernel.hpp>
#include <smart/vision/vision.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using smart::vision::ExecutionRoute;

class ConfigGuard
{
  public:
    ConfigGuard()
        : saved_(smart::global_config())
    {
    }

    ~ConfigGuard()
    {
        smart::global_config() = saved_;
        smart::vision::clear_adaptive_route_cache();
    }

  private:
    smart::Config saved_;
};

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<std::uint8_t> reference_threshold(const std::vector<std::uint8_t>& source,
                                              smart::vision::ThresholdOptions options)
{
    std::vector<std::uint8_t> output(source.size());
    for (std::size_t index = 0; index < source.size(); ++index)
    {
        const bool above = source[index] > options.threshold;
        output[index] = options.mode == smart::vision::ThresholdMode::Binary
            ? (above ? options.maximum_value : std::uint8_t{0})
            : (above ? std::uint8_t{0} : options.maximum_value);
    }
    return output;
}


void validate_native_kernel_dispatch()
{
    using smart::vision::detail::ThresholdKernelKind;
    const ThresholdKernelKind kind = smart::vision::detail::selected_threshold_kernel();
    const char* name = smart::vision::detail::threshold_kernel_name(kind);
    require(name != nullptr && name[0] != '\0',
            "native threshold kernel did not report an identity");
#if defined(_M_X64) || defined(__x86_64__)
    require(smart::vision::detail::threshold_kernel_uses_explicit_simd(),
            "x86-64 validation requires an authenticated SSE2 or AVX2 kernel");
#endif
    std::cout << "SmartParallel native threshold kernel: " << name << '\n';
}

void validate_native_kernel_shapes()
{
    constexpr std::size_t width = 37;
    constexpr std::size_t height = 11;
    constexpr std::size_t stride = 48;
    const smart::vision::ThresholdOptions binary{
        100, 211, smart::vision::ThresholdMode::Binary};
    const smart::vision::ThresholdOptions inverse{
        100, 211, smart::vision::ThresholdMode::BinaryInverse};

    std::vector<std::uint8_t> source(stride * height, 73);
    for (std::size_t row = 0; row < height; ++row)
        for (std::size_t column = 0; column < width; ++column)
            source[row * stride + column] = static_cast<std::uint8_t>(
                (row * 29u + column * 17u) & 0xffu);

    std::vector<std::uint8_t> contiguous_source(width * height);
    for (std::size_t row = 0; row < height; ++row)
        std::copy_n(source.data() + row * stride,
                    width,
                    contiguous_source.data() + row * width);

    for (const auto options : {binary, inverse})
    {
        const bool is_inverse = options.mode == smart::vision::ThresholdMode::BinaryInverse;
        const auto contiguous_expected = reference_threshold(contiguous_source, options);

        std::vector<std::uint8_t> contiguous_output(contiguous_source.size(), 9);
        if (is_inverse)
        {
            smart::vision::detail::threshold_u8_contiguous_disjoint<true>(
                contiguous_source.data(),
                contiguous_output.data(),
                contiguous_output.size(),
                options.threshold,
                options.maximum_value);
        }
        else
        {
            smart::vision::detail::threshold_u8_contiguous_disjoint<false>(
                contiguous_source.data(),
                contiguous_output.data(),
                contiguous_output.size(),
                options.threshold,
                options.maximum_value);
        }
        require(contiguous_output == contiguous_expected,
                "disjoint contiguous native kernel produced incorrect output");

        std::vector<std::uint8_t> in_place = contiguous_source;
        if (is_inverse)
        {
            smart::vision::detail::threshold_u8_contiguous_in_place<true>(
                in_place.data(), in_place.size(), options.threshold, options.maximum_value);
        }
        else
        {
            smart::vision::detail::threshold_u8_contiguous_in_place<false>(
                in_place.data(), in_place.size(), options.threshold, options.maximum_value);
        }
        require(in_place == contiguous_expected,
                "in-place contiguous native kernel produced incorrect output");

        std::vector<std::uint8_t> strided_output(stride * height, 0xa5);
        if (is_inverse)
        {
            smart::vision::detail::threshold_u8_rows_disjoint<true>(
                source.data(), stride, strided_output.data(), stride, width,
                0, height, options.threshold, options.maximum_value);
        }
        else
        {
            smart::vision::detail::threshold_u8_rows_disjoint<false>(
                source.data(), stride, strided_output.data(), stride, width,
                0, height, options.threshold, options.maximum_value);
        }
        for (std::size_t row = 0; row < height; ++row)
        {
            for (std::size_t column = 0; column < width; ++column)
            {
                const bool above = source[row * stride + column] > options.threshold;
                const std::uint8_t expected = is_inverse
                    ? (above ? std::uint8_t{0} : options.maximum_value)
                    : (above ? options.maximum_value : std::uint8_t{0});
                require(strided_output[row * stride + column] == expected,
                        "disjoint strided native kernel produced incorrect output");
            }
            for (std::size_t column = width; column < stride; ++column)
            {
                require(strided_output[row * stride + column] == 0xa5,
                        "disjoint strided kernel modified row padding");
            }
        }

        std::vector<std::uint8_t> strided_in_place = source;
        if (is_inverse)
        {
            smart::vision::detail::threshold_u8_rows_in_place<true>(
                strided_in_place.data(), stride, width, 0, height,
                options.threshold, options.maximum_value);
        }
        else
        {
            smart::vision::detail::threshold_u8_rows_in_place<false>(
                strided_in_place.data(), stride, width, 0, height,
                options.threshold, options.maximum_value);
        }
        for (std::size_t row = 0; row < height; ++row)
        {
            for (std::size_t column = 0; column < width; ++column)
                require(strided_in_place[row * stride + column]
                            == strided_output[row * stride + column],
                        "in-place strided native kernel produced incorrect output");
            for (std::size_t column = width; column < stride; ++column)
                require(strided_in_place[row * stride + column]
                            == source[row * stride + column],
                        "in-place strided kernel modified row padding");
        }
    }
}

void validate_native_kernel_edge_values()
{
    std::vector<std::uint8_t> source(513);
    for (std::size_t index = 0; index < source.size(); ++index)
        source[index] = static_cast<std::uint8_t>(index & 0xffu);

    for (const std::uint8_t threshold : {std::uint8_t{0}, std::uint8_t{1},
                                          std::uint8_t{127}, std::uint8_t{254},
                                          std::uint8_t{255}})
    {
        for (const std::uint8_t maximum : {std::uint8_t{0}, std::uint8_t{1},
                                            std::uint8_t{211}, std::uint8_t{255}})
        {
            for (const auto mode : {smart::vision::ThresholdMode::Binary,
                                    smart::vision::ThresholdMode::BinaryInverse})
            {
                const smart::vision::ThresholdOptions options{threshold, maximum, mode};
                const auto expected = reference_threshold(source, options);
                std::vector<std::uint8_t> output(source.size(), 17);
                if (mode == smart::vision::ThresholdMode::BinaryInverse)
                {
                    smart::vision::detail::threshold_u8_contiguous_disjoint<true>(
                        source.data(), output.data(), output.size(), threshold, maximum);
                }
                else
                {
                    smart::vision::detail::threshold_u8_contiguous_disjoint<false>(
                        source.data(), output.data(), output.size(), threshold, maximum);
                }
                require(output == expected,
                        "native SIMD threshold failed an edge-value disjoint case");

                std::vector<std::uint8_t> in_place = source;
                if (mode == smart::vision::ThresholdMode::BinaryInverse)
                {
                    smart::vision::detail::threshold_u8_contiguous_in_place<true>(
                        in_place.data(), in_place.size(), threshold, maximum);
                }
                else
                {
                    smart::vision::detail::threshold_u8_contiguous_in_place<false>(
                        in_place.data(), in_place.size(), threshold, maximum);
                }
                require(in_place == expected,
                        "native SIMD threshold failed an edge-value in-place case");
            }
        }
    }
}

void validate_selector_learning()
{
    using namespace smart::vision::detail;
    AdaptiveRouteSelector selector;
    RouteSelectorSettings settings;
    settings.equivalence_ratio = 1.05;
    settings.warmup_samples = 1;
    settings.minimum_samples = 5;
    settings.sample_window = 9;
    settings.initial_revalidate_interval = 100;
    settings.revalidate_interval = 100;

    RouteProfileKey key;
    key.operation = 1;
    key.pixel_bucket = 1024;
    const std::vector<ExecutionRoute> candidates{
        ExecutionRoute::NativeSequential,
        ExecutionRoute::OpenCV,
        ExecutionRoute::NativeThreadPool};

    std::size_t sequential_samples = 0;
    std::size_t opencv_samples = 0;
    std::size_t thread_pool_samples = 0;
    bool reached_stable = false;
    for (std::size_t invocation = 0; invocation < 64; ++invocation)
    {
        const auto selection = selector.select(key, candidates, settings);
        if (!selection.probe)
        {
            require(selection.stable && selection.route == ExecutionRoute::OpenCV,
                    "median selector did not retain the fastest route");
            reached_stable = true;
            break;
        }

        double elapsed = 1'000.0; // Priming samples must not influence ranking.
        if (!selection.warmup)
        {
            switch (selection.route)
            {
                case ExecutionRoute::NativeSequential:
                    elapsed = ++sequential_samples == 1 ? 100.0 : 10.0;
                    break;
                case ExecutionRoute::OpenCV:
                    elapsed = ++opencv_samples == 1 ? 50.0 : 5.0;
                    break;
                case ExecutionRoute::NativeThreadPool:
                    elapsed = ++thread_pool_samples == 1 ? 80.0 : 8.0;
                    break;
                default:
                    throw std::runtime_error("unexpected selector candidate");
            }
        }
        selector.complete(selection, elapsed, settings);
    }
    require(reached_stable, "median selector did not finish its learning phase");

    RouteMeasurementSnapshot snapshot;
    require(selector.snapshot(key, snapshot), "selector snapshot was unavailable");
    require(snapshot.stable_route == ExecutionRoute::OpenCV,
            "selector snapshot did not report OpenCV as the learned route");
    require(snapshot.routes.size() == 3, "selector snapshot lost candidates");
    for (std::size_t index = 0; index < snapshot.routes.size(); ++index)
    {
        require(snapshot.warmups[index] == 1,
                "selector did not prime every route exactly once");
        require(snapshot.samples[index] == 5,
                "selector did not collect five measured samples per route");
    }
    require(snapshot.elapsed_ms[0] == 10.0 && snapshot.elapsed_ms[1] == 5.0
                && snapshot.elapsed_ms[2] == 8.0,
            "selector did not rank routes by observed median");

    // Revalidation uses a bounded recent window and can promote a changed winner.
    AdaptiveRouteSelector revalidation_selector;
    RouteSelectorSettings revalidation_settings;
    revalidation_settings.warmup_samples = 0;
    revalidation_settings.minimum_samples = 1;
    revalidation_settings.sample_window = 1;
    revalidation_settings.holdout_samples = 0;
    revalidation_settings.initial_revalidate_interval = 2;
    revalidation_settings.revalidate_interval = 8;
    RouteProfileKey revalidation_key;
    revalidation_key.operation = 2;
    const std::vector<ExecutionRoute> revalidation_candidates{
        ExecutionRoute::NativeSequential,
        ExecutionRoute::OpenCV};

    for (std::size_t invocation = 0; invocation < 2; ++invocation)
    {
        const auto selection = revalidation_selector.select(
            revalidation_key, revalidation_candidates, revalidation_settings);
        require(selection.probe, "initial revalidation profile was not measured");
        revalidation_selector.complete(
            selection,
            selection.route == ExecutionRoute::OpenCV ? 5.0 : 10.0,
            revalidation_settings);
    }
    auto selection = revalidation_selector.select(
        revalidation_key, revalidation_candidates, revalidation_settings);
    require(selection.stable && selection.route == ExecutionRoute::OpenCV,
            "initial revalidation profile selected the wrong route");
    selection = revalidation_selector.select(
        revalidation_key, revalidation_candidates, revalidation_settings);
    require(selection.probe && selection.revalidation
                && selection.revalidation_stable_sample
                && selection.revalidation_stage == 1
                && selection.route == ExecutionRoute::OpenCV,
            "selector did not begin current-context revalidation with the stable route");
    revalidation_selector.complete(selection, 20.0, revalidation_settings);
    for (std::uint8_t stage : {std::uint8_t{2}, std::uint8_t{3}})
    {
        selection = revalidation_selector.select(
            revalidation_key, revalidation_candidates, revalidation_settings);
        require(selection.probe && selection.revalidation
                    && !selection.revalidation_stable_sample
                    && selection.revalidation_stage == stage
                    && selection.route == ExecutionRoute::NativeSequential,
                "selector did not collect paired challenger evidence");
        revalidation_selector.complete(selection, 2.0, revalidation_settings);
    }
    selection = revalidation_selector.select(
        revalidation_key, revalidation_candidates, revalidation_settings);
    require(selection.probe && selection.revalidation
                && selection.revalidation_stable_sample
                && selection.revalidation_stage == 4
                && selection.route == ExecutionRoute::OpenCV,
            "selector did not close current-context revalidation with the stable route");
    revalidation_selector.complete(selection, 20.0, revalidation_settings);
    selection = revalidation_selector.select(
        revalidation_key, revalidation_candidates, revalidation_settings);
    require(selection.stable && selection.route == ExecutionRoute::NativeSequential,
            "selector did not promote the faster current-context route");

    RouteProfileKey other = key;
    other.operation = 3;
    selection = selector.select(other, candidates, settings);
    require(selection.probe,
            "selector incorrectly shared decisions across operations");
}


void validate_selector_decisive_revalidation()
{
    using namespace smart::vision::detail;
    AdaptiveRouteSelector selector;
    RouteSelectorSettings settings;
    settings.warmup_samples = 0;
    settings.minimum_samples = 5;
    settings.sample_window = 9;
    settings.holdout_samples = 0;
    settings.initial_revalidate_interval = 2;
    settings.revalidate_interval = 8;

    RouteProfileKey key;
    key.operation = 44;
    const std::vector<ExecutionRoute> candidates{
        ExecutionRoute::NativeSequential,
        ExecutionRoute::OpenCV};

    for (std::size_t invocation = 0; invocation < 10; ++invocation)
    {
        const auto selection = selector.select(key, candidates, settings);
        require(selection.probe && !selection.revalidation,
                "robust selector did not collect its initial profile");
        selector.complete(selection,
                          selection.route == ExecutionRoute::OpenCV ? 5.0 : 10.0,
                          settings);
    }

    auto selection = selector.select(key, candidates, settings);
    require(selection.stable && !selection.probe
                && selection.route == ExecutionRoute::OpenCV,
            "robust selector did not stabilize on OpenCV");
    selection = selector.select(key, candidates, settings);
    require(selection.probe && selection.revalidation
                && selection.revalidation_stage == 1
                && selection.route == ExecutionRoute::OpenCV,
            "robust selector did not begin paired revalidation on the stable route");
    selector.complete(selection, 20.0, settings);
    selection = selector.select(key, candidates, settings);
    require(selection.probe && selection.revalidation
                && selection.revalidation_stage == 2
                && selection.route == ExecutionRoute::NativeSequential,
            "robust selector did not collect challenger A");
    selector.complete(selection, 2.0, settings);
    selection = selector.select(key, candidates, settings);
    require(selection.probe && selection.revalidation
                && selection.revalidation_stage == 3
                && selection.route == ExecutionRoute::NativeSequential,
            "robust selector did not collect challenger B");
    selector.complete(selection, 2.1, settings);
    selection = selector.select(key, candidates, settings);
    require(selection.probe && selection.revalidation
                && selection.revalidation_stage == 4
                && selection.route == ExecutionRoute::OpenCV,
            "robust selector did not collect stable B");
    selector.complete(selection, 20.5, settings);
    selection = selector.select(key, candidates, settings);
    require(selection.stable && selection.route == ExecutionRoute::NativeSequential,
            "paired revalidation did not promote the current winner");
}


void validate_selector_robust_learning()
{
    using namespace smart::vision::detail;

    auto drive = [](AdaptiveRouteSelector& selector,
                    const RouteProfileKey& key,
                    const std::vector<ExecutionRoute>& candidates,
                    const RouteSelectorSettings& settings,
                    const std::function<double(const RouteSelection&, std::size_t)>& timing)
    {
        std::size_t invocation = 0;
        for (; invocation < 160; ++invocation)
        {
            const RouteSelection selection = selector.select(key, candidates, settings);
            if (!selection.probe)
            {
                require(selection.stable, "robust selector returned an unstabilized fallback");
                return selection.route;
            }
            selector.complete(selection, timing(selection, invocation), settings);
        }
        throw std::runtime_error("robust selector did not converge within 160 calls");
    };

    RouteSelectorSettings settings;
    settings.warmup_samples = 0;
    settings.minimum_samples = 3;
    settings.sample_window = 11;
    settings.holdout_samples = 2;
    settings.maximum_verification_failures = 2;
    settings.initial_revalidate_interval = 1000;
    settings.revalidate_interval = 1000;

    // A single exceptionally fast external-provider sample must not beat the
    // consistently faster native route.
    {
        AdaptiveRouteSelector selector;
        RouteProfileKey key;
        key.operation = 501;
        const std::vector<ExecutionRoute> candidates{
            ExecutionRoute::NativeSequential,
            ExecutionRoute::OpenCV,
            ExecutionRoute::NativeThreadPool};
        std::array<std::size_t, 3> training_counts{};
        std::array<std::size_t, 3> holdout_counts{};
        const auto winner = drive(selector, key, candidates, settings,
            [&](const RouteSelection& selection, std::size_t)
            {
                const std::size_t index = selection.route == ExecutionRoute::NativeSequential
                    ? 0 : selection.route == ExecutionRoute::OpenCV ? 1 : 2;
                if (selection.holdout)
                {
                    static const double values[3][2]{{80.0, 81.0}, {142.0, 145.0}, {101.0, 102.0}};
                    return values[index][holdout_counts[index]++ % 2];
                }
                static const double values[3][3]{{80.0, 79.0, 81.0}, {30.0, 150.0, 145.0}, {100.0, 101.0, 99.0}};
                return values[index][training_counts[index]++ % 3];
            });
        require(winner == ExecutionRoute::NativeSequential,
                "outlier resistance selected the transiently fast OpenCV route");
    }

    // A provisional training winner that loses an independent holdout must be
    // reopened and replaced after bounded additional evidence.
    {
        AdaptiveRouteSelector selector;
        RouteProfileKey key;
        key.operation = 502;
        const std::vector<ExecutionRoute> candidates{
            ExecutionRoute::NativeSequential, ExecutionRoute::OpenCV};
        std::array<std::size_t, 2> training_counts{};
        std::array<std::size_t, 2> holdout_counts{};
        const auto winner = drive(selector, key, candidates, settings,
            [&](const RouteSelection& selection, std::size_t)
            {
                const std::size_t index = selection.route == ExecutionRoute::NativeSequential ? 0 : 1;
                if (selection.holdout)
                {
                    // First holdout reverses the training winner. Subsequent
                    // holdouts continue to confirm Native Sequential.
                    static const double native_values[]{70.0, 72.0, 70.0, 71.0, 70.0, 71.0};
                    static const double opencv_values[]{150.0, 152.0, 150.0, 151.0, 150.0, 151.0};
                    return index == 0
                        ? native_values[holdout_counts[index]++ % 6]
                        : opencv_values[holdout_counts[index]++ % 6];
                }
                static const double initial[2][7]{
                    {90.0, 91.0, 92.0, 70.0, 71.0, 70.0, 71.0},
                    {80.0, 81.0, 82.0, 150.0, 151.0, 150.0, 151.0}};
                const std::size_t ordinal = std::min<std::size_t>(training_counts[index]++, 6);
                return initial[index][ordinal];
            });
        require(winner == ExecutionRoute::NativeSequential,
                "holdout reversal did not replace the provisional OpenCV winner");
        RouteMeasurementSnapshot snapshot;
        require(selector.snapshot(key, snapshot), "holdout reversal snapshot missing");
        require(snapshot.verification_failures >= 1,
                "holdout reversal was not recorded in selector telemetry");
    }

    // Statistically equivalent candidates resolve by semantic preference, not
    // input vector order.
    for (bool reverse : {false, true})
    {
        AdaptiveRouteSelector selector;
        RouteProfileKey key;
        key.operation = reverse ? 504 : 503;
        std::vector<ExecutionRoute> candidates{
            ExecutionRoute::NativeSequential,
            ExecutionRoute::NativeThreadPool,
            ExecutionRoute::OpenCV};
        if (reverse)
            std::reverse(candidates.begin(), candidates.end());
        std::array<std::size_t, 6> counts{};
        const auto winner = drive(selector, key, candidates, settings,
            [&](const RouteSelection& selection, std::size_t)
            {
                const std::size_t route = static_cast<std::size_t>(selection.route);
                const double base = selection.route == ExecutionRoute::NativeSequential ? 100.0
                    : selection.route == ExecutionRoute::NativeThreadPool ? 98.0 : 99.0;
                return base + static_cast<double>((counts[route]++) % 3) * 0.1;
            });
        require(winner == ExecutionRoute::NativeSequential,
                "equivalent routes did not resolve to Native Sequential");
    }
}


void validate_selector_distribution_shift()
{
    using namespace smart::vision::detail;
    AdaptiveRouteSelector selector;
    RouteSelectorSettings settings;
    settings.warmup_samples = 0;
    settings.minimum_samples = 3;
    settings.sample_window = 7;
    settings.holdout_samples = 0;
    settings.initial_revalidate_interval = 1000;
    settings.revalidate_interval = 1000;
    settings.drift_required_samples = 2;
    settings.drift_ratio = 1.25;
    settings.drift_absolute_ms = 1.0;

    RouteProfileKey key;
    key.operation = 601;
    const std::vector<ExecutionRoute> candidates{
        ExecutionRoute::NativeSequential, ExecutionRoute::OpenCV};

    // Initial regime: OpenCV is decisively faster.
    for (std::size_t invocation = 0; invocation < 6; ++invocation)
    {
        const auto selection = selector.select(key, candidates, settings);
        require(selection.probe && !selection.revalidation,
                "distribution-shift profile did not collect initial evidence");
        selector.complete(selection,
                          selection.route == ExecutionRoute::OpenCV ? 30.0 : 80.0,
                          settings);
    }
    auto selection = selector.select(key, candidates, settings);
    require(selection.stable && selection.route == ExecutionRoute::OpenCV,
            "distribution-shift profile did not initially learn OpenCV");

    // New regime: sparse stable-route sentinels observe a major slowdown.
    settings.force_stable_sample = true;
    for (std::size_t sentinel = 0; sentinel < 2; ++sentinel)
    {
        selection = selector.select(key, candidates, settings);
        require(selection.probe && selection.drift_sample
                    && selection.route == ExecutionRoute::OpenCV,
                "distribution-shift sentinel did not time the stable route");
        selector.complete(selection, 120.0, settings);
    }
    settings.force_stable_sample = false;

    RouteMeasurementSnapshot snapshot;
    require(selector.snapshot(key, snapshot),
            "distribution-shift telemetry was unavailable");
    require(snapshot.drift_detected && snapshot.revalidation_active,
            "distribution shift did not trigger current-context revalidation");

    // Stable A came from the second sentinel. Complete challenger A/B and stable B.
    for (std::uint8_t stage : {std::uint8_t{2}, std::uint8_t{3}})
    {
        selection = selector.select(key, candidates, settings);
        require(selection.revalidation && selection.revalidation_stage == stage
                    && selection.route == ExecutionRoute::NativeSequential,
                "distribution-shift revalidation missed challenger evidence");
        selector.complete(selection, stage == 2 ? 81.0 : 83.0, settings);
    }
    selection = selector.select(key, candidates, settings);
    require(selection.revalidation && selection.revalidation_stage == 4
                && selection.route == ExecutionRoute::OpenCV,
            "distribution-shift revalidation missed final stable evidence");
    selector.complete(selection, 121.0, settings);

    selection = selector.select(key, candidates, settings);
    require(selection.stable && selection.route == ExecutionRoute::NativeSequential,
            "selector did not adapt to the changed performance regime");
    require(selector.snapshot(key, snapshot),
            "post-switch distribution telemetry was unavailable");
    require(snapshot.route_switch_count == 1
                && snapshot.last_revalidation_stable_ms > 120.0
                && snapshot.last_revalidation_challenger_ms >= 82.0
                && snapshot.last_revalidation_challenger_ms <= 83.0,
            "current-context revalidation telemetry was inconsistent");
}

void validate_selector_single_flight()
{
    using namespace smart::vision::detail;
    AdaptiveRouteSelector selector;
    RouteSelectorSettings settings;
    RouteProfileKey key;
    key.operation = 7;
    const std::vector<ExecutionRoute> candidates{
        ExecutionRoute::NativeSequential,
        ExecutionRoute::OpenCV};

    const auto first = selector.select(key, candidates, settings);
    require(first.probe, "first selector call was not a probe");
    const auto concurrent = selector.select(key, candidates, settings);
    require(!concurrent.probe, "selector allowed duplicate concurrent probes");
    selector.complete(concurrent, 0.001, settings);
    RouteMeasurementSnapshot snapshot;
    require(selector.snapshot(key, snapshot), "single-flight selector snapshot failed");
    require(snapshot.samples[0] == 0 && snapshot.warmups[0] == 0,
            "concurrent fallback polluted route learning");
    selector.cancel(first);
    const auto retry = selector.select(key, candidates, settings);
    require(retry.probe, "cancelled selector probe could not be retried");
}

void validate_selector_bounded_eviction()
{
    using namespace smart::vision::detail;
    AdaptiveRouteSelector selector;
    RouteSelectorSettings settings;
    settings.maximum_entries = 16;
    settings.minimum_samples = 1;
    const std::vector<ExecutionRoute> candidates{ExecutionRoute::NativeSequential};

    RouteProfileKey first;
    first.operation = 100;
    const std::size_t target_shard = RouteProfileKeyHasher{}(first) % 16;
    RouteProfileKey second = first;
    for (std::uint64_t operation = 101; operation < 10'000; ++operation)
    {
        second.operation = operation;
        if (RouteProfileKeyHasher{}(second) % 16 == target_shard)
            break;
    }
    require(second.operation != first.operation,
            "could not construct same-shard route-cache keys");

    auto selection = selector.select(first, candidates, settings);
    selector.complete(selection, 1.0, settings);
    selection = selector.select(second, candidates, settings);
    selector.complete(selection, 1.0, settings);

    RouteMeasurementSnapshot snapshot;
    require(!selector.snapshot(first, snapshot),
            "bounded selector did not evict its least-recently-used entry");
    require(selector.snapshot(second, snapshot),
            "bounded selector evicted the newly inserted entry");
}

void validate_threshold_routes()
{
    constexpr std::size_t width = 257;
    constexpr std::size_t height = 131;
    std::vector<std::uint8_t> source(width * height);
    for (std::size_t index = 0; index < source.size(); ++index)
        source[index] = static_cast<std::uint8_t>((index * 37u + index / 11u) & 0xffu);

    const smart::vision::ThresholdOptions options{
        127,
        231,
        smart::vision::ThresholdMode::Binary};
    const std::vector<std::uint8_t> expected = reference_threshold(source, options);
    const auto source_view = smart::vision::make_contiguous_image_view(
        static_cast<const std::uint8_t*>(source.data()), width, height);

    std::vector<ExecutionRoute> routes{
        ExecutionRoute::NativeSequential,
        ExecutionRoute::NativeThreadPool,
        ExecutionRoute::NativeStaticThread};
    if (smart::execution_backend_available(smart::ExecutionEngineType::OneTbb))
        routes.push_back(ExecutionRoute::NativeOneTbb);
    if (smart::vision::opencv_available())
        routes.push_back(ExecutionRoute::OpenCV);

    for (ExecutionRoute route : routes)
    {
        std::vector<std::uint8_t> output(source.size(), 17);
        smart::vision::threshold(
            source_view,
            smart::vision::make_contiguous_image_view(output.data(), width, height),
            options,
            {route, 4});
        require(output == expected, "forced threshold route produced incorrect output");
        const auto report = smart::vision::last_decision_report();
        require(report.selected_route == route, "forced threshold route was not reported");
        require(report.backend_authenticated, "forced threshold route was not authenticated");

        std::vector<std::uint8_t> in_place = source;
        smart::vision::threshold(
            smart::vision::make_contiguous_image_view(
                static_cast<const std::uint8_t*>(in_place.data()), width, height),
            smart::vision::make_contiguous_image_view(in_place.data(), width, height),
            options,
            {route, 4});
        require(in_place == expected, "forced in-place threshold route produced incorrect output");
    }

    smart::vision::clear_adaptive_route_cache();
    smart::vision::DecisionReport automatic_report;
    bool learned = false;
    for (int invocation = 0; invocation < 64; ++invocation)
    {
        std::vector<std::uint8_t> output(source.size());
        smart::vision::threshold(
            source_view,
            smart::vision::make_contiguous_image_view(output.data(), width, height),
            options,
            {ExecutionRoute::Auto, 4});
        require(output == expected, "automatic threshold route produced incorrect output");
        automatic_report = smart::vision::last_decision_report();
        if (automatic_report.learned_route && !automatic_report.exploration_probe
            && !automatic_report.revalidation_probe)
        {
            learned = true;
            break;
        }
    }
    require(learned, "automatic threshold route did not stabilize within 64 calls");
    require(automatic_report.adaptive_selection_enabled,
            "automatic threshold did not enable adaptive route selection");
    require(automatic_report.backend_authenticated,
            "automatic threshold route was not authenticated");
    require(automatic_report.learned_route,
            "automatic threshold route did not reach a stable learned decision");
    require(automatic_report.cache_hit,
            "learned automatic threshold did not use the hot route cache");
    require(!automatic_report.execution_timed,
            "stable hot-route execution retained internal timing overhead");

    ConfigGuard config_guard;
    smart::global_config().enable_vision_adaptive_routes = false;
    std::vector<std::uint8_t> disabled_output(source.size());
    smart::vision::threshold(
        source_view,
        smart::vision::make_contiguous_image_view(
            disabled_output.data(), width, height),
        options,
        {ExecutionRoute::Auto, 4});
    require(disabled_output == expected,
            "disabled adaptive route selection produced incorrect output");
    const auto disabled_report = smart::vision::last_decision_report();
    require(disabled_report.selected_route == ExecutionRoute::NativeSequential,
            "disabled adaptive route selection did not use the native sequential fallback");
    require(!disabled_report.adaptive_selection_enabled,
            "disabled adaptive route selection still entered the selector");
}

void validate_publication_stability_window()
{
    ConfigGuard config_guard;
    auto& config = smart::global_config();
    config.vision_route_initial_revalidate_interval = 64;
    config.vision_route_revalidate_interval = 128;
    ++config.vision_route_policy_generation;
    smart::vision::clear_adaptive_route_cache();

    constexpr std::size_t width = 4096;
    constexpr std::size_t height = 8;
    std::vector<std::uint8_t> source(width * height);
    for (std::size_t index = 0; index < source.size(); ++index)
        source[index] = static_cast<std::uint8_t>((index * 37u + 11u) & 0xffu);
    const smart::vision::ThresholdOptions options{
        127, 255, smart::vision::ThresholdMode::Binary};
    const auto expected = reference_threshold(source, options);
    const auto source_view = smart::vision::make_contiguous_image_view(
        static_cast<const std::uint8_t*>(source.data()), width, height);

    smart::vision::DecisionReport learned_report;
    bool learned = false;
    std::vector<std::uint8_t> output(source.size());
    for (std::size_t invocation = 0; invocation < 64; ++invocation)
    {
        smart::vision::threshold(
            source_view,
            smart::vision::make_contiguous_image_view(output.data(), width, height),
            options,
            {ExecutionRoute::Auto, 4});
        require(output == expected,
                "publication stability learning produced incorrect output");
        learned_report = smart::vision::last_decision_report();
        if (learned_report.learned_route && !learned_report.exploration_probe
            && !learned_report.revalidation_probe)
        {
            learned = true;
            break;
        }
    }
    require(learned,
            "publication stability profile did not learn within 64 calls");
    const ExecutionRoute learned_route = learned_report.selected_route;
    config.vision_route_pause_maintenance = true;

    for (std::size_t repetition = 0; repetition < 31; ++repetition)
    {
        smart::vision::threshold(
            source_view,
            smart::vision::make_contiguous_image_view(output.data(), width, height),
            options,
            {ExecutionRoute::Auto, 4});
        const auto report = smart::vision::last_decision_report();
        require(output == expected,
                "publication stability window produced incorrect output");
        require(report.learned_route,
                "publication stability window lost its learned route");
        require(!report.exploration_probe && !report.revalidation_probe
                    && !report.drift_probe,
                "publication stability window unexpectedly contained a probe");
        require(report.selected_route == learned_route,
                "publication stability window changed the learned route");
    }
}

void validate_strided_and_edge_cases()
{
    smart::vision::threshold({}, {});

    std::uint8_t source_pixel = 128;
    std::uint8_t destination_pixel = 0;
    smart::vision::threshold(
        smart::vision::make_contiguous_image_view(
            static_cast<const std::uint8_t*>(&source_pixel), 1, 1),
        smart::vision::make_contiguous_image_view(&destination_pixel, 1, 1),
        {127, 255, smart::vision::ThresholdMode::Binary},
        {ExecutionRoute::NativeSequential, 1});
    require(destination_pixel == 255, "one-pixel threshold failed");

    std::vector<std::uint8_t> in_place{0, 100, 127, 128, 255};
    smart::vision::threshold(
        smart::vision::make_contiguous_image_view(
            static_cast<const std::uint8_t*>(in_place.data()), in_place.size(), 1),
        smart::vision::make_contiguous_image_view(in_place.data(), in_place.size(), 1),
        {127, 255, smart::vision::ThresholdMode::Binary},
        {ExecutionRoute::NativeSequential, 1});
    require((in_place == std::vector<std::uint8_t>{0, 0, 0, 255, 255}),
            "in-place threshold failed");

    constexpr std::size_t width = 63;
    constexpr std::size_t height = 17;
    constexpr std::size_t stride = 80;
    std::vector<std::uint8_t> source(stride * height, 13);
    std::vector<std::uint8_t> output(stride * height, 99);
    for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < width; ++x)
            source[y * stride + x] = static_cast<std::uint8_t>((x * 9 + y * 17) & 0xff);

    std::vector<ExecutionRoute> strided_routes{
        ExecutionRoute::NativeThreadPool,
        ExecutionRoute::NativeStaticThread};
    if (smart::execution_backend_available(smart::ExecutionEngineType::OneTbb))
        strided_routes.push_back(ExecutionRoute::NativeOneTbb);
    if (smart::vision::opencv_available())
        strided_routes.push_back(ExecutionRoute::OpenCV);

    for (ExecutionRoute route : strided_routes)
    {
        std::fill(output.begin(), output.end(), std::uint8_t{99});
        smart::vision::threshold(
            smart::vision::make_image_view(
                static_cast<const std::uint8_t*>(source.data()), width, height, stride),
            smart::vision::make_image_view(output.data(), width, height, stride),
            {100, 200, smart::vision::ThresholdMode::BinaryInverse},
            {route, 3});

        for (std::size_t y = 0; y < height; ++y)
        {
            for (std::size_t x = 0; x < width; ++x)
            {
                const std::uint8_t expected = source[y * stride + x] > 100 ? 0 : 200;
                require(output[y * stride + x] == expected, "strided threshold mismatch");
            }
            for (std::size_t x = width; x < stride; ++x)
            {
                require(output[y * stride + x] == 99,
                        "strided threshold modified row padding");
            }
        }
    }

    bool threw = false;
    try
    {
        smart::vision::threshold(
            smart::vision::make_image_view(
                static_cast<const std::uint8_t*>(source.data()), width, height, width - 1),
            smart::vision::make_image_view(output.data(), width, height, stride));
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    require(threw, "invalid source stride was not rejected");

    threw = false;
    try
    {
        smart::vision::threshold(
            smart::vision::make_image_view(
                static_cast<const std::uint8_t*>(source.data()), width, height, stride),
            smart::vision::make_image_view(output.data(), width - 1, height, stride));
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    require(threw, "mismatched image dimensions were not rejected");

    threw = false;
    try
    {
        smart::vision::threshold(
            smart::vision::make_image_view(
                static_cast<const std::uint8_t*>(source.data()), width, height, stride),
            smart::vision::make_image_view(output.data(), width, height, stride),
            {127, 255, static_cast<smart::vision::ThresholdMode>(99)});
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    require(threw, "invalid threshold mode was not rejected");

    threw = false;
    try
    {
        smart::vision::threshold(
            smart::vision::make_image_view(
                static_cast<const std::uint8_t*>(source.data()), width, height, stride),
            smart::vision::make_image_view(output.data(), width, height, stride),
            {},
            {static_cast<ExecutionRoute>(99), 1});
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    require(threw, "invalid execution route was not rejected");

    std::vector<std::uint8_t> overlap(width + 8, 200);
    threw = false;
    try
    {
        smart::vision::threshold(
            smart::vision::make_contiguous_image_view(
                static_cast<const std::uint8_t*>(overlap.data()), width, 1),
            smart::vision::make_contiguous_image_view(overlap.data() + 1, width, 1));
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    require(threw, "partially overlapping buffers were not rejected");

    if (!smart::execution_backend_available(smart::ExecutionEngineType::OneTbb))
    {
        threw = false;
        try
        {
            smart::vision::threshold(
                smart::vision::make_image_view(
                    static_cast<const std::uint8_t*>(source.data()), width, height, stride),
                smart::vision::make_image_view(output.data(), width, height, stride),
                {},
                {ExecutionRoute::NativeOneTbb, 2});
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        require(threw, "unavailable forced oneTBB route did not fail clearly");
    }

    if (!smart::vision::opencv_available())
    {
        threw = false;
        try
        {
            smart::vision::threshold(
                smart::vision::make_image_view(
                    static_cast<const std::uint8_t*>(source.data()), width, height, stride),
                smart::vision::make_image_view(output.data(), width, height, stride),
                {},
                {ExecutionRoute::OpenCV, 1});
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        require(threw, "unavailable forced OpenCV route did not fail clearly");
    }
}

void validate_nested_auto_fallback()
{
    std::vector<std::uint8_t> source(128, 200);
    std::vector<std::uint8_t> output(128, 0);
    std::atomic<bool> nested_fallback{false};
    smart::parallel_for(std::size_t{0}, std::size_t{1}, [&](std::size_t)
    {
        smart::vision::threshold(
            smart::vision::make_contiguous_image_view(
                static_cast<const std::uint8_t*>(source.data()), source.size(), 1),
            smart::vision::make_contiguous_image_view(output.data(), output.size(), 1));
        nested_fallback.store(
            smart::vision::last_decision_report().nested_fallback,
            std::memory_order_relaxed);
    });
    require(nested_fallback.load(std::memory_order_relaxed),
            "nested Auto vision call did not use the safe native fallback");
}
} // namespace

int main()
{
    try
    {
        validate_native_kernel_dispatch();
        validate_native_kernel_shapes();
        validate_native_kernel_edge_values();
        validate_selector_learning();
        validate_selector_decisive_revalidation();
        validate_selector_robust_learning();
        validate_selector_distribution_shift();
        validate_selector_single_flight();
        validate_selector_bounded_eviction();
        validate_threshold_routes();
        validate_publication_stability_window();
        validate_strided_and_edge_cases();
        validate_nested_auto_fallback();
        std::cout << "SmartParallel v1.5 adaptive execution-route validation passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "SmartParallel v1.5 validation failed: " << exception.what() << '\n';
        return 1;
    }
}
