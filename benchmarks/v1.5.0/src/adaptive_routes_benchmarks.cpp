#include <smart/core/config.hpp>
#include <smart/execution/runtime_capabilities.hpp>
#include <smart/hardware/hardware_characteristics.hpp>
#include <smart/vision/vision.hpp>
#include <smart/vision/detail/threshold_kernel.hpp>

#include <opencv2/core.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
using smart::vision::ExecutionRoute;

constexpr std::uint8_t padding_sentinel = 0xa5;

struct Preset
{
    const char* name;
    std::size_t width;
    std::size_t height;
    std::size_t extra_stride;
};

struct Case
{
    std::string name;
    bool smartparallel = false;
    ExecutionRoute route = ExecutionRoute::Auto;
};

struct ValidationResult
{
    std::uint64_t checksum = 1469598103934665603ull;
    std::size_t mismatch_count = 0;
};

class BenchmarkConfigGuard
{
  public:
    explicit BenchmarkConfigGuard(std::size_t repetitions)
        : saved_(smart::global_config())
    {
        const std::size_t maximum = std::numeric_limits<std::size_t>::max();
        if (repetitions > maximum - 16)
            throw std::invalid_argument("Repetition count is too large");
        auto& config = smart::global_config();
        config.vision_route_initial_revalidate_interval = 8;
        config.vision_route_revalidate_interval = 128;
        config.vision_route_drift_sample_interval = 8;
        config.vision_route_drift_required_samples = 2;
        config.vision_route_drift_ratio = 1.25;
        config.vision_route_drift_absolute_ms = 0.002;
        config.vision_route_pause_maintenance = false;
        ++config.vision_route_policy_generation;
        smart::vision::refresh_provider_state();
    }

    ~BenchmarkConfigGuard()
    {
        smart::global_config() = saved_;
        smart::vision::clear_adaptive_route_cache();
    }

    void pause_maintenance() noexcept
    {
        smart::global_config().vision_route_pause_maintenance = true;
    }

    void resume_maintenance() noexcept
    {
        smart::global_config().vision_route_pause_maintenance = false;
    }

  private:
    smart::Config saved_;
};

struct Sample
{
    std::string preset;
    std::string implementation;
    std::string phase;
    std::size_t repetition = 0;
    std::size_t measurement_ordinal = 0;
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t stride = 0;
    bool contiguous = true;
    std::uintptr_t source_address = 0;
    std::uintptr_t destination_address = 0;
    std::size_t source_alignment = 1;
    std::size_t destination_alignment = 1;
    std::size_t learning_invocations = 0;
    std::size_t deployment_invocations = 0;
    std::size_t route_switch_count = 0;
    std::string pair_order;
    std::size_t pair_position = 0;
    std::size_t batch_iterations = 1;
    std::uint64_t batch_total_ns = 0;
    std::uint64_t duration_ns = 0;
    std::uint64_t checksum = 0;
    std::size_t mismatch_count = 0;
    std::string requested_route;
    std::string selected_route;
    std::size_t worker_budget = 1;
    std::size_t participants = 1;
    std::size_t chunks = 1;
    bool learned = false;
    bool probe = false;
    bool exploration_probe = false;
    bool holdout_probe = false;
    bool revalidation_probe = false;
    bool drift_probe = false;
    bool correctness = false;
    bool authentication = false;
};

struct LearningSnapshot
{
    std::string preset;
    smart::vision::RouteTrainingReport report;
};

std::size_t alignment_class(const void* pointer) noexcept
{
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
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

ValidationResult validate_output(const std::vector<std::uint8_t>& data,
                                 const std::vector<std::uint8_t>& expected,
                                 std::size_t height,
                                 std::size_t stride)
{
    ValidationResult result;
    for (std::size_t y = 0; y < height; ++y)
    {
        for (std::size_t x = 0; x < stride; ++x)
        {
            const std::size_t index = y * stride + x;
            const std::uint8_t value = data[index];
            result.checksum ^= value;
            result.checksum *= 1099511628211ull;
            if (value != expected[index])
                ++result.mismatch_count;
        }
    }
    return result;
}

void fill_source(std::vector<std::uint8_t>& source,
                 std::size_t height,
                 std::size_t stride)
{
    std::uint64_t state = 0x9e3779b97f4a7c15ull;
    for (std::size_t y = 0; y < height; ++y)
    {
        for (std::size_t x = 0; x < stride; ++x)
        {
            state ^= state << 13u;
            state ^= state >> 7u;
            state ^= state << 17u;
            source[y * stride + x] = static_cast<std::uint8_t>(
                (state + x * 31u + y * 17u) & 0xffu);
        }
    }
}

// Deliberately independent of SmartParallel internals. This is the external
// compiler-generated performance oracle and must remain a plain reference loop.
void direct_sequential(const std::vector<std::uint8_t>& source,
                       std::vector<std::uint8_t>& output,
                       std::size_t width,
                       std::size_t height,
                       std::size_t stride,
                       std::uint8_t threshold,
                       std::uint8_t maximum)
{
    for (std::size_t row = 0; row < height; ++row)
    {
        const std::uint8_t* source_row = source.data() + row * stride;
        std::uint8_t* destination_row = output.data() + row * stride;
        for (std::size_t column = 0; column < width; ++column)
        {
            destination_row[column] = source_row[column] > threshold
                ? maximum
                : std::uint8_t{0};
        }
    }
}

void opencv_threshold(const std::vector<std::uint8_t>& source,
                      std::vector<std::uint8_t>& output,
                      std::size_t width,
                      std::size_t height,
                      std::size_t stride,
                      std::uint8_t threshold,
                      std::uint8_t maximum)
{
    cv::Mat source_mat(static_cast<int>(height),
                       static_cast<int>(width),
                       CV_8UC1,
                       const_cast<std::uint8_t*>(source.data()),
                       stride);
    cv::Mat destination_mat(static_cast<int>(height),
                            static_cast<int>(width),
                            CV_8UC1,
                            output.data(),
                            stride);
    cv::threshold(source_mat,
                  destination_mat,
                  static_cast<double>(threshold),
                  static_cast<double>(maximum),
                  cv::THRESH_BINARY);
}

std::uint64_t time_call(const std::function<void()>& function)
{
    const auto start = Clock::now();
    function();
    const auto end = Clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

std::uint64_t time_batch(const std::function<void()>& function,
                         std::size_t iterations)
{
    const auto start = Clock::now();
    for (std::size_t iteration = 0; iteration < iterations; ++iteration)
        function();
    const auto end = Clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

std::vector<std::vector<std::size_t>> balanced_orders(std::size_t count)
{
    std::vector<std::vector<std::size_t>> result;
    if (count == 0)
        return result;
    std::vector<std::size_t> base(count);
    base[0] = 0;
    for (std::size_t position = 1; position < count; ++position)
    {
        base[position] = (position % 2u) == 1u
            ? (position + 1u) / 2u
            : count - position / 2u;
    }
    for (std::size_t shift = 0; shift < count; ++shift)
    {
        std::vector<std::size_t> order(count);
        for (std::size_t position = 0; position < count; ++position)
            order[position] = (base[position] + shift) % count;
        result.push_back(order);
    }
    if ((count % 2u) != 0u)
    {
        const std::size_t original = result.size();
        for (std::size_t index = 0; index < original; ++index)
        {
            std::vector<std::size_t> reversed = result[index];
            std::reverse(reversed.begin(), reversed.end());
            result.push_back(std::move(reversed));
        }
    }
    return result;
}

std::vector<Case> cases()
{
    std::vector<Case> result{
        {"opencv_api", false, ExecutionRoute::OpenCV},
        {"direct_sequential", false, ExecutionRoute::NativeSequential},
        {"smart_auto", true, ExecutionRoute::Auto},
        {"smart_native_sequential", true, ExecutionRoute::NativeSequential},
        {"smart_native_thread_pool", true, ExecutionRoute::NativeThreadPool},
        {"smart_native_static_thread", true, ExecutionRoute::NativeStaticThread},
        {"smart_opencv", true, ExecutionRoute::OpenCV}};
    if (smart::execution_backend_available(smart::ExecutionEngineType::OneTbb))
        result.push_back({"smart_native_one_tbb", true, ExecutionRoute::NativeOneTbb});
    return result;
}

const Case& forced_case_for_route(const std::vector<Case>& benchmark_cases,
                                  ExecutionRoute route)
{
    const auto found = std::find_if(
        benchmark_cases.begin(), benchmark_cases.end(), [route](const Case& value)
        {
            return value.smartparallel && value.route == route;
        });
    if (found == benchmark_cases.end())
        throw std::runtime_error("No forced benchmark case matches the learned route");
    return *found;
}

void execute_case(const Case& benchmark_case,
                  const std::vector<std::uint8_t>& source,
                  std::vector<std::uint8_t>& output,
                  const Preset& preset,
                  std::size_t stride,
                  std::size_t worker_budget,
                  std::uint8_t threshold_value,
                  std::uint8_t maximum_value)
{
    if (benchmark_case.name == "opencv_api")
    {
        opencv_threshold(source, output, preset.width, preset.height, stride,
                         threshold_value, maximum_value);
        return;
    }
    if (benchmark_case.name == "direct_sequential")
    {
        direct_sequential(source, output, preset.width, preset.height, stride,
                          threshold_value, maximum_value);
        return;
    }

    smart::vision::threshold(
        smart::vision::make_image_view(
            static_cast<const std::uint8_t*>(source.data()),
            preset.width,
            preset.height,
            stride),
        smart::vision::make_image_view(
            output.data(), preset.width, preset.height, stride),
        {threshold_value, maximum_value, smart::vision::ThresholdMode::Binary},
        {benchmark_case.route, worker_budget});
}

Sample make_sample(const Case& benchmark_case,
                   const Preset& preset,
                   std::size_t stride,
                   std::string phase,
                   std::size_t repetition,
                   std::size_t measurement_ordinal,
                   std::size_t learning_invocations,
                   std::size_t deployment_invocations,
                   const std::vector<std::uint8_t>& source,
                   const std::vector<std::uint8_t>& output,
                   std::uint64_t duration_ns,
                   const ValidationResult& validation,
                   std::uint64_t expected_checksum,
                   std::size_t worker_budget,
                   std::string pair_order = {},
                   std::size_t pair_position = 0,
                   std::size_t batch_iterations = 1,
                   std::uint64_t batch_total_ns = 0)
{
    Sample sample;
    sample.preset = preset.name;
    sample.implementation = benchmark_case.name;
    sample.phase = std::move(phase);
    sample.repetition = repetition;
    sample.measurement_ordinal = measurement_ordinal;
    sample.width = preset.width;
    sample.height = preset.height;
    sample.stride = stride;
    sample.contiguous = stride == preset.width;
    sample.source_address = reinterpret_cast<std::uintptr_t>(source.data());
    sample.destination_address = reinterpret_cast<std::uintptr_t>(output.data());
    sample.source_alignment = alignment_class(source.data());
    sample.destination_alignment = alignment_class(output.data());
    sample.learning_invocations = learning_invocations;
    sample.deployment_invocations = deployment_invocations;
    sample.pair_order = std::move(pair_order);
    sample.pair_position = pair_position;
    sample.batch_iterations = batch_iterations;
    sample.batch_total_ns = batch_total_ns == 0 ? duration_ns : batch_total_ns;
    sample.duration_ns = duration_ns;
    sample.checksum = validation.checksum;
    sample.mismatch_count = validation.mismatch_count;
    sample.correctness = validation.mismatch_count == 0
        && validation.checksum == expected_checksum;
    sample.worker_budget = worker_budget;

    if (benchmark_case.smartparallel)
    {
        const smart::vision::DecisionReport report = smart::vision::last_decision_report();
        sample.requested_route = smart::vision::execution_route_name(report.requested_route);
        sample.selected_route = smart::vision::execution_route_name(report.selected_route);
        sample.worker_budget = report.worker_budget;
        sample.participants = report.participant_count;
        sample.chunks = report.chunk_count;
        sample.learned = report.learned_route;
        sample.exploration_probe = report.exploration_probe;
        sample.holdout_probe = report.holdout_probe;
        sample.revalidation_probe = report.revalidation_probe;
        sample.drift_probe = report.drift_probe;
        sample.probe = sample.exploration_probe || sample.revalidation_probe
            || sample.drift_probe;
        const auto training = smart::vision::last_route_training_report();
        sample.route_switch_count = training.route_switch_count;
        sample.authentication = report.backend_authenticated;
    }
    else
    {
        sample.requested_route = benchmark_case.name;
        sample.selected_route = benchmark_case.name;
        sample.authentication = true;
    }
    return sample;
}

void write_raw(const std::filesystem::path& path,
               const std::vector<Sample>& samples,
               std::size_t repetitions,
               std::size_t logical_threads)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output)
        throw std::runtime_error("Could not open raw CSV output");
    output << "schema_version,benchmark_version,operation,preset,implementation,phase,"
              "repetition_index,measurement_ordinal,width,height,channels,element_type,"
              "stride_bytes,contiguous,source_address,destination_address,source_alignment,"
              "destination_alignment,shared_destination_id,learning_invocations,deployment_invocations,route_switch_count,pair_order,pair_position,batch_iterations,batch_total_ns,native_kernel,duration_ns,"
              "checksum,mismatch_count,requested_route,selected_route,worker_budget,"
              "participant_count,chunk_count,learned_route,probe,exploration_probe,holdout_probe,"
              "revalidation_probe,drift_probe,correctness_pass,backend_authentication_pass,opencv_version,"
              "opencv_threads,logical_threads,requested_repetitions\n";
    for (const Sample& sample : samples)
    {
        output << "6,1.5.0,threshold_u8_binary," << sample.preset << ','
               << sample.implementation << ',' << sample.phase << ',' << sample.repetition
               << ',' << sample.measurement_ordinal << ',' << sample.width << ','
               << sample.height << ",1,uint8," << sample.stride << ','
               << (sample.contiguous ? 1 : 0) << ',' << sample.source_address << ','
               << sample.destination_address << ',' << sample.source_alignment << ','
               << sample.destination_alignment << ",shared_per_preset,"
               << sample.learning_invocations << ',' << sample.deployment_invocations << ','
               << sample.route_switch_count << ',' << sample.pair_order << ','
               << sample.pair_position << ',' << sample.batch_iterations << ','
               << sample.batch_total_ns << ','
               << smart::vision::detail::threshold_kernel_name(
                      smart::vision::detail::selected_threshold_kernel())
               << ',' << sample.duration_ns << ','
               << sample.checksum << ',' << sample.mismatch_count << ','
               << sample.requested_route << ',' << sample.selected_route << ','
               << sample.worker_budget << ',' << sample.participants << ',' << sample.chunks
               << ',' << (sample.learned ? 1 : 0) << ',' << (sample.probe ? 1 : 0) << ','
               << (sample.exploration_probe ? 1 : 0) << ','
               << (sample.holdout_probe ? 1 : 0) << ','
               << (sample.revalidation_probe ? 1 : 0) << ','
               << (sample.drift_probe ? 1 : 0) << ','
               << (sample.correctness ? 1 : 0) << ','
               << (sample.authentication ? 1 : 0) << ',' << CV_VERSION << ','
               << cv::getNumThreads() << ',' << logical_threads << ',' << repetitions << '\n';
    }
}

void write_learning(const std::filesystem::path& raw_path,
                    const std::vector<LearningSnapshot>& snapshots)
{
    const std::filesystem::path path = raw_path.parent_path()
        / "v1.5.0_adaptive_routes_learning.csv";
    std::ofstream output(path);
    if (!output)
        throw std::runtime_error("Could not open learning telemetry output");
    output << "schema_version,preset,stable,stable_route,provisional_route,holdout_active,revalidation_active,drift_detected,verification_failures,route_switch_count,drift_strikes,revalidation_challenger,training_baseline_ms,current_baseline_ms,last_revalidation_stable_ms,last_revalidation_challenger_ms,route,median_ms,mad_ms,minimum_ms,maximum_ms,current_median_ms,sample_count,current_sample_count,warmup_count,holdout_sample_count,active\n";
    for (const LearningSnapshot& snapshot : snapshots)
    {
        for (const smart::vision::RouteTrainingEntry& route : snapshot.report.routes)
        {
            output << "2," << snapshot.preset << ',' << (snapshot.report.stable ? 1 : 0)
                   << ',' << smart::vision::execution_route_name(snapshot.report.stable_route)
                   << ',' << smart::vision::execution_route_name(snapshot.report.provisional_route)
                   << ',' << (snapshot.report.holdout_active ? 1 : 0)
                   << ',' << (snapshot.report.revalidation_active ? 1 : 0)
                   << ',' << (snapshot.report.drift_detected ? 1 : 0)
                   << ',' << snapshot.report.verification_failures
                   << ',' << snapshot.report.route_switch_count
                   << ',' << snapshot.report.drift_strikes
                   << ',' << smart::vision::execution_route_name(snapshot.report.revalidation_challenger)
                   << ',' << snapshot.report.training_baseline_ms
                   << ',' << snapshot.report.current_baseline_ms
                   << ',' << snapshot.report.last_revalidation_stable_ms
                   << ',' << snapshot.report.last_revalidation_challenger_ms
                   << ',' << smart::vision::execution_route_name(route.route)
                   << ',' << route.median_ms << ',' << route.mad_ms
                   << ',' << route.minimum_ms << ',' << route.maximum_ms
                   << ',' << route.current_median_ms
                   << ',' << route.sample_count << ',' << route.current_sample_count
                   << ',' << route.warmup_count
                   << ',' << route.holdout_sample_count << ',' << (route.active ? 1 : 0)
                   << '\n';
        }
    }
}

void write_environment(const std::filesystem::path& raw_path,
                       std::size_t worker_budget,
                       std::size_t logical_threads)
{
    const std::filesystem::path path = raw_path.parent_path()
        / "v1.5.0_adaptive_routes_environment.txt";
    std::ofstream output(path);
    if (!output)
        throw std::runtime_error("Could not open environment metadata output");
    output << "SmartParallel benchmark version: 1.5.0\n"
           << "Raw schema version: 6\n"
           << "Operation: threshold_u8_binary\n"
           << "OpenCV version: " << CV_VERSION << "\n"
           << "OpenCV threads: " << cv::getNumThreads() << "\n"
           << "OpenCV OpenCL enabled: " << (cv::ocl::useOpenCL() ? "yes" : "no") << "\n"
           << "SmartParallel worker budget: " << worker_budget << "\n"
           << "Native threshold kernel: "
           << smart::vision::detail::threshold_kernel_name(
                  smart::vision::detail::selected_threshold_kernel()) << "\n"
           << "Explicit SIMD kernel active: "
           << (smart::vision::detail::threshold_kernel_uses_explicit_simd() ? "yes" : "no")
           << "\n"
           << "Logical threads: " << logical_threads << "\n"
           << "Destination policy: one identical allocation per preset for every route\n"
           << "Timing policy: destination reset occurs outside timed sections; steady routes use balanced Williams orders\n"
           << "Dispatch-overhead policy: batched adjacent Auto/forced ABBA and BAAB blocks\n"
           << "Learning policy: balanced successive elimination with independent holdout verification\n"
           << "Adaptation policy: sparse stable-route drift sentinels and current-context ABBA revalidation\n"
           << "Reference policy: direct_sequential is independent of SmartParallel internals\n"
           << "Steady-state policy: maintenance is paused only after deployment settling completes\n"
           << "Periodic revalidation and drift detection remain enabled in production and are covered by "
              "the deterministic v1.5 validation target.\n\n"
           << "OpenCV build information\n"
           << "========================\n"
           << cv::getBuildInformation();
}

void require_results_valid(const std::vector<Sample>& samples)
{
    for (const Sample& sample : samples)
    {
        if (!sample.correctness)
        {
            throw std::runtime_error(
                "Benchmark correctness failure in " + sample.implementation
                + " (mismatches=" + std::to_string(sample.mismatch_count) + ")");
        }
        if (!sample.authentication)
            throw std::runtime_error("Backend authentication failure in " + sample.implementation);
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2 || argc > 3)
        {
            std::cerr << "Usage: smartparallel_v150_adaptive_routes_benchmarks <raw.csv> [odd repetitions]\n";
            return 2;
        }
        const std::size_t repetitions = argc == 3
            ? static_cast<std::size_t>(std::stoull(argv[2]))
            : 31;
        if (repetitions == 0 || repetitions % 2 == 0)
            throw std::invalid_argument("Repetition count must be a positive odd number");
        if (!smart::vision::opencv_available())
            throw std::runtime_error("The v1.5 publication benchmark requires OpenCV");

        cv::ocl::setUseOpenCL(false);
        BenchmarkConfigGuard benchmark_config(repetitions);
        const std::size_t worker_budget = std::max<std::size_t>(1, smart::hardware_threads());
        const std::uint8_t threshold_value = 127;
        const std::uint8_t maximum_value = 255;
        const std::vector<Preset> presets{
            {"tiny_320x240", 320, 240, 0},
            {"small_640x480", 640, 480, 0},
            {"medium_1920x1080", 1920, 1080, 0},
            {"medium_1920x1080_roi", 1920, 1080, 64},
            {"large_3840x2160", 3840, 2160, 0},
            {"very_large_7680x4320", 7680, 4320, 0}};
        const std::vector<Case> benchmark_cases = cases();
        const auto automatic_iterator = std::find_if(
            benchmark_cases.begin(), benchmark_cases.end(), [](const Case& value)
            {
                return value.name == "smart_auto";
            });
        if (automatic_iterator == benchmark_cases.end())
            throw std::runtime_error("Auto benchmark case is missing");
        const Case& automatic_case = *automatic_iterator;
        std::vector<Sample> samples;
        std::vector<LearningSnapshot> learning_snapshots;
        const std::vector<std::vector<std::size_t>> steady_orders =
            balanced_orders(benchmark_cases.size());

        std::cout << "SmartParallel v1.5 Adaptive Execution Routes benchmark\n"
                  << "OpenCV: " << CV_VERSION << "\n"
                  << "OpenCV threads: " << cv::getNumThreads() << "\n"
                  << "SmartParallel worker budget: " << worker_budget << "\n"
                  << "Native threshold kernel: "
                  << smart::vision::detail::threshold_kernel_name(
                         smart::vision::detail::selected_threshold_kernel()) << "\n"
                  << "Explicit SIMD kernel active: "
                  << (smart::vision::detail::threshold_kernel_uses_explicit_simd()
                          ? "yes" : "no") << "\n"
                  << "Repetitions: " << repetitions << "\n"
                  << "Publication policy: adapt under balanced deployment load, then pause maintenance\n";

        for (const Preset& preset : presets)
        {
            const std::size_t stride = preset.width + preset.extra_stride;
            std::vector<std::uint8_t> source(stride * preset.height);
            fill_source(source, preset.height, stride);
            std::vector<std::uint8_t> expected(stride * preset.height, padding_sentinel);
            direct_sequential(source, expected, preset.width, preset.height, stride,
                              threshold_value, maximum_value);
            const ValidationResult expected_validation = validate_output(
                expected, expected, preset.height, stride);
            const std::uint64_t expected_checksum = expected_validation.checksum;

            // Every implementation uses this exact allocation and alignment.
            std::vector<std::uint8_t> output(stride * preset.height, padding_sentinel);

            benchmark_config.resume_maintenance();
            smart::vision::clear_adaptive_route_cache();
            std::fill(output.begin(), output.end(), padding_sentinel);
            const std::uint64_t cold_ns = time_call([&]
            {
                execute_case(automatic_case, source, output, preset, stride,
                             worker_budget, threshold_value, maximum_value);
            });
            samples.push_back(make_sample(
                automatic_case, preset, stride, "cold", 0, 0, 0, 0, source, output,
                cold_ns, validate_output(output, expected, preset.height, stride),
                expected_checksum, worker_budget));

            constexpr std::size_t maximum_learning_invocations = 160;
            std::size_t learning_invocations = 0;
            bool learned = false;
            ExecutionRoute learned_route = ExecutionRoute::Auto;
            for (; learning_invocations < maximum_learning_invocations;
                 ++learning_invocations)
            {
                std::fill(output.begin(), output.end(), padding_sentinel);
                execute_case(automatic_case, source, output, preset, stride,
                             worker_budget, threshold_value, maximum_value);
                const auto report = smart::vision::last_decision_report();
                if (report.learned_route && !report.exploration_probe
                    && !report.revalidation_probe && !report.drift_probe)
                {
                    ++learning_invocations;
                    learned_route = report.selected_route;
                    learned = true;
                    break;
                }
            }
            if (!learned)
                throw std::runtime_error(
                    "Automatic route learning did not stabilize within 160 invocations");
            const smart::vision::RouteTrainingReport initial_training_report =
                smart::vision::last_route_training_report();
            if (!initial_training_report.available || !initial_training_report.stable
                || initial_training_report.stable_route != learned_route)
                throw std::runtime_error("Automatic route training telemetry was unavailable or inconsistent");

            // Enter the deployment-like balanced/interleaved regime with normal
            // maintenance enabled. The selector must detect any ranking drift,
            // complete current-context ABBA revalidation, and then remain clean
            // for a bounded streak before publication measurements are frozen.
            constexpr std::size_t maximum_deployment_auto_invocations = 64;
            constexpr std::size_t required_clean_streak = 6;
            const std::size_t minimum_deployment_auto_invocations =
                smart::global_config().vision_route_drift_sample_interval * 2 + 4;
            std::size_t deployment_invocations = 0;
            std::size_t clean_streak = 0;
            bool deployment_settled = false;
            ExecutionRoute settled_route = learned_route;
            for (std::size_t block_index = 0;
                 deployment_invocations < maximum_deployment_auto_invocations
                     && !deployment_settled;
                 ++block_index)
            {
                const std::vector<std::size_t>& order =
                    steady_orders[block_index % steady_orders.size()];
                for (std::size_t ordinal = 0; ordinal < benchmark_cases.size(); ++ordinal)
                {
                    const Case& benchmark_case = benchmark_cases[order[ordinal]];
                    std::fill(output.begin(), output.end(), padding_sentinel);
                    execute_case(benchmark_case, source, output, preset, stride,
                                 worker_budget, threshold_value, maximum_value);
                    const ValidationResult validation = validate_output(
                        output, expected, preset.height, stride);
                    if (validation.mismatch_count != 0
                        || validation.checksum != expected_checksum)
                    {
                        throw std::runtime_error(
                            "Deployment settling produced incorrect output");
                    }
                    if (benchmark_case.name != "smart_auto")
                        continue;

                    ++deployment_invocations;
                    const auto report = smart::vision::last_decision_report();
                    const auto training = smart::vision::last_route_training_report();
                    const bool clean = report.learned_route
                        && !report.exploration_probe && !report.holdout_probe
                        && !report.revalidation_probe && !report.drift_probe
                        && training.available && training.stable
                        && !training.revalidation_active;
                    if (clean)
                    {
                        if (report.selected_route == settled_route)
                            ++clean_streak;
                        else
                        {
                            settled_route = report.selected_route;
                            clean_streak = 1;
                        }
                    }
                    else
                    {
                        clean_streak = 0;
                    }
                    deployment_settled = deployment_invocations
                            >= minimum_deployment_auto_invocations
                        && clean_streak >= required_clean_streak;
                    if (deployment_settled)
                        break;
                }
            }
            if (!deployment_settled)
                throw std::runtime_error(
                    "Automatic route did not settle after deployment regime change");

            learned_route = settled_route;
            const smart::vision::RouteTrainingReport settled_training_report =
                smart::vision::last_route_training_report();
            if (!settled_training_report.available || !settled_training_report.stable
                || settled_training_report.revalidation_active
                || settled_training_report.stable_route != learned_route)
            {
                throw std::runtime_error(
                    "Settled route telemetry was unavailable or inconsistent");
            }
            learning_snapshots.push_back({preset.name, settled_training_report});
            benchmark_config.pause_maintenance();

            std::size_t measurement_ordinal = 0;
            for (std::size_t repetition = 0; repetition < repetitions; ++repetition)
            {
                const std::vector<std::size_t>& order =
                    steady_orders[repetition % steady_orders.size()];
                for (std::size_t ordinal = 0; ordinal < benchmark_cases.size(); ++ordinal)
                {
                    const Case& benchmark_case = benchmark_cases[order[ordinal]];
                    std::fill(output.begin(), output.end(), padding_sentinel);
                    const std::uint64_t duration_ns = time_call([&]
                    {
                        execute_case(benchmark_case, source, output, preset, stride,
                                     worker_budget, threshold_value, maximum_value);
                    });
                    samples.push_back(make_sample(
                        benchmark_case, preset, stride, "steady_state", repetition,
                        measurement_ordinal++, learning_invocations, deployment_invocations, source, output,
                        duration_ns,
                        validate_output(output, expected, preset.height, stride),
                        expected_checksum,
                        worker_budget));
                }
            }

            // Batched adjacent ABBA/BAAB blocks amortize scheduler and clock noise.
            const Case& selected_forced_case = forced_case_for_route(
                benchmark_cases, learned_route);
            std::fill(output.begin(), output.end(), padding_sentinel);
            const std::uint64_t calibration_auto = time_call([&]
            {
                execute_case(automatic_case, source, output, preset, stride,
                             worker_budget, threshold_value, maximum_value);
            });
            std::fill(output.begin(), output.end(), padding_sentinel);
            const std::uint64_t calibration_forced = time_call([&]
            {
                execute_case(selected_forced_case, source, output, preset, stride,
                             worker_budget, threshold_value, maximum_value);
            });
            constexpr std::uint64_t target_batch_ns = 8'000'000;
            const std::uint64_t calibration = std::max<std::uint64_t>(
                1, std::max(calibration_auto, calibration_forced));
            const std::size_t batch_iterations = std::max<std::size_t>(
                1, std::min<std::size_t>(100'000,
                    static_cast<std::size_t>((target_batch_ns + calibration - 1) / calibration)));
            for (std::size_t repetition = 0; repetition < repetitions; ++repetition)
            {
                const bool auto_outer = (repetition % 2u) == 0u;
                const std::string pair_order = auto_outer
                    ? "auto_forced_forced_auto" : "forced_auto_auto_forced";
                const Case* pair_cases[4] = {
                    auto_outer ? &automatic_case : &selected_forced_case,
                    auto_outer ? &selected_forced_case : &automatic_case,
                    auto_outer ? &selected_forced_case : &automatic_case,
                    auto_outer ? &automatic_case : &selected_forced_case};
                for (std::size_t position = 0; position < 4; ++position)
                {
                    const Case& benchmark_case = *pair_cases[position];
                    std::fill(output.begin(), output.end(), padding_sentinel);
                    const std::uint64_t batch_total_ns = time_batch([&]
                    {
                        execute_case(benchmark_case, source, output, preset, stride,
                                     worker_budget, threshold_value, maximum_value);
                    }, batch_iterations);
                    const std::uint64_t duration_ns = batch_total_ns / batch_iterations;
                    samples.push_back(make_sample(
                        benchmark_case, preset, stride, "dispatch_batch", repetition,
                        measurement_ordinal++, learning_invocations, deployment_invocations, source, output,
                        duration_ns,
                        validate_output(output, expected, preset.height, stride),
                        expected_checksum, worker_budget, pair_order, position + 1,
                        batch_iterations, batch_total_ns));
                }
            }
            std::cout << "Completed " << preset.name
                      << " (learning calls: " << learning_invocations
                      << ", deployment calls: " << deployment_invocations << ")\n";
            benchmark_config.resume_maintenance();
        }

        require_results_valid(samples);
        const std::size_t logical_threads =
            smart::hardware_characteristics().logical_threads;
        write_raw(argv[1], samples, repetitions, logical_threads);
        write_learning(argv[1], learning_snapshots);
        write_environment(argv[1], worker_budget, logical_threads);
        std::cout << "Raw results: " << argv[1] << '\n';
        std::cout << "Environment: "
                  << (std::filesystem::path(argv[1]).parent_path()
                      / "v1.5.0_adaptive_routes_environment.txt")
                  << '\n';
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "v1.5 adaptive-routes benchmark failed: " << exception.what() << '\n';
        return 1;
    }
}
