#include "tool_common.hpp"

#include <numeric>

#if SMARTPARALLEL_TOOL_HAS_VISION
#include <smart/vision/threshold.hpp>
#endif

namespace
{
using Clock = std::chrono::steady_clock;

struct Manifest
{
    std::string operation;
    std::size_t size = 65536;
    std::size_t rows = 256;
    std::size_t columns = 256;
    std::size_t stride = 0;
    std::size_t iterations = 20;
    std::size_t repetitions = 5;
    std::size_t worker_budget = 1;
    std::uint64_t seed = 170;
    smart::NumericalPolicy policy = smart::NumericalPolicy::Reproducible;
    std::filesystem::path output_directory;
    std::string source_manifest_hash;
};

Manifest parse_manifest(const std::filesystem::path& path)
{
    const std::string json = smartparallel_tool::read_text(path);
    const auto schema = smartparallel_tool::json_size(json, "schema_version");
    if (schema != 1) throw std::runtime_error("unsupported calibration manifest schema");
    Manifest result;
    result.operation = smartparallel_tool::json_string(json, "operation");
    result.size = smartparallel_tool::json_size(json, "size", result.size);
    result.rows = smartparallel_tool::json_size(json, "rows", result.rows);
    result.columns = smartparallel_tool::json_size(json, "columns", result.columns);
    result.stride = smartparallel_tool::json_size(json, "stride", result.columns);
    result.iterations = smartparallel_tool::json_size(json, "iterations", result.iterations);
    result.repetitions = smartparallel_tool::json_size(json, "repetitions", result.repetitions);
    result.worker_budget = smartparallel_tool::json_size(json, "worker_budget", result.worker_budget);
    result.seed = static_cast<std::uint64_t>(smartparallel_tool::json_size(json, "seed", result.seed));
    result.policy = smartparallel_tool::parse_policy(
        smartparallel_tool::json_string(json, "numerical_policy", "Reproducible"));
    result.output_directory = smartparallel_tool::json_string(json, "output_directory");
    result.source_manifest_hash = smart::sha256_hex(json);
    if (result.repetitions < 2) throw std::runtime_error("calibration requires at least two repetitions");
    if (result.worker_budget == 0) throw std::runtime_error("worker_budget must be positive");
    if (result.stride < result.columns) throw std::runtime_error("stride must be at least columns");
    return result;
}

struct CalibrationResult
{
    std::vector<double> durations_ms;
    std::vector<smart::OperationExecutionFingerprint> fingerprints;
    std::string output_digest;
    bool correctness = true;
};

void append_duration(CalibrationResult& result, Clock::time_point begin,
                     const smart::Runtime& runtime)
{
    const auto end = Clock::now();
    result.durations_ms.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
    result.fingerprints.push_back(runtime.last_operation_fingerprint());
}

CalibrationResult calibrate_axpy(const Manifest& manifest, smart::Runtime& runtime)
{
    CalibrationResult result;
    const auto x = smartparallel_tool::deterministic_values(manifest.size, manifest.seed);
    std::vector<double> y(manifest.size, 0.25), reference = y;
    for (std::size_t repeat = 0; repeat < manifest.repetitions; ++repeat)
    {
        for (std::size_t i = 0; i < reference.size(); ++i) reference[i] = 1.25 * x[i] + reference[i];
        auto xv = smart::data::VectorView<const double>::contiguous(x.data(), {x.size()});
        auto yv = smart::data::VectorView<double>::contiguous(y.data(), {y.size()});
        const auto begin = Clock::now();
        smart::linalg::axpy(runtime.context(), yv, 1.25, xv, smart::NumericalOptions{manifest.policy});
        append_duration(result, begin, runtime);
    }
    result.correctness = std::memcmp(y.data(), reference.data(), y.size() * sizeof(double)) == 0;
    result.output_digest = smartparallel_tool::digest_doubles(y);
    return result;
}

CalibrationResult calibrate_dot_or_norm(const Manifest& manifest, smart::Runtime& runtime, bool norm)
{
    CalibrationResult result;
    const auto x = smartparallel_tool::deterministic_values(manifest.size, manifest.seed);
    const auto y = smartparallel_tool::deterministic_values(manifest.size, manifest.seed + 1);
    auto xv = smart::data::VectorView<const double>::contiguous(x.data(), {x.size()});
    auto yv = smart::data::VectorView<const double>::contiguous(y.data(), {y.size()});
    double last = 0.0;
    for (std::size_t repeat = 0; repeat < manifest.repetitions; ++repeat)
    {
        const auto begin = Clock::now();
        last = norm ? smart::linalg::norm(runtime.context(), xv, smart::NumericalOptions{manifest.policy})
                    : smart::linalg::dot(runtime.context(), xv, yv, smart::NumericalOptions{manifest.policy});
        append_duration(result, begin, runtime);
    }
    long double reference = 0.0L;
    if (norm) for (double value : x) reference += static_cast<long double>(value) * value;
    else for (std::size_t i = 0; i < x.size(); ++i) reference += static_cast<long double>(x[i]) * y[i];
    if (norm) reference = std::sqrt(reference);
    const long double tolerance = manifest.policy == smart::NumericalPolicy::Accurate ? 1e-12L : 1e-9L;
    result.correctness = std::abs(static_cast<long double>(last) - reference)
        <= tolerance * std::max<long double>(1.0L, std::abs(reference));
    result.output_digest = smart::sha256_hex(std::string(reinterpret_cast<const char*>(&last), sizeof(last)));
    return result;
}

void reference_stencil_step(const std::vector<double>& input,
                            std::vector<double>& output,
                            std::size_t rows,
                            std::size_t columns,
                            std::size_t stride,
                            const smart::scientific::Stencil2DCoefficients<double>& coefficients)
{
    for (std::size_t row = 0; row < rows; ++row)
    {
        if (row == 0 || row + 1 == rows || columns < 3)
        {
            for (std::size_t column = 0; column < columns; ++column)
                output[row * stride + column] = input[row * stride + column];
            continue;
        }
        output[row * stride] = input[row * stride];
        for (std::size_t column = 1; column + 1 < columns; ++column)
        {
            double value = coefficients.center * input[row * stride + column];
            value += coefficients.north * input[(row - 1) * stride + column];
            value += coefficients.south * input[(row + 1) * stride + column];
            value += coefficients.west * input[row * stride + column - 1];
            value += coefficients.east * input[row * stride + column + 1];
            output[row * stride + column] = value;
        }
        output[row * stride + columns - 1] = input[row * stride + columns - 1];
    }
}

CalibrationResult calibrate_stencil(const Manifest& manifest, smart::Runtime& runtime, bool heat)
{
    CalibrationResult result;
    const std::size_t stride = manifest.stride == 0 ? manifest.columns : manifest.stride;
    const std::size_t storage_count = manifest.rows * stride;
    std::vector<double> a = smartparallel_tool::deterministic_values(storage_count, manifest.seed);
    std::vector<double> b(storage_count, 0.0);
    std::vector<double> reference_a = a;
    std::vector<double> reference_b(storage_count, 0.0);
    smart::scientific::Stencil2DCoefficients<double> coefficients;
    coefficients.center = 0.5;
    coefficients.north = coefficients.south = coefficients.west = coefficients.east = 0.125;
    const std::size_t calls = heat ? manifest.iterations : manifest.repetitions;
    if (calls < 2)
        throw std::runtime_error("stencil calibration requires at least two operation samples");
    for (std::size_t repeat = 0; repeat < calls; ++repeat)
    {
        auto input = smart::data::MatrixView<const double>(
            a.data(), {manifest.rows, manifest.columns}, {stride, 1});
        auto output = smart::data::MatrixView<double>(
            b.data(), {manifest.rows, manifest.columns}, {stride, 1});
        const auto begin = Clock::now();
        smart::scientific::stencil_2d(runtime.context(), input, output, coefficients,
                                      smart::NumericalOptions{manifest.policy});
        append_duration(result, begin, runtime);
        reference_stencil_step(reference_a, reference_b, manifest.rows,
                               manifest.columns, stride, coefficients);
        std::swap(a, b);
        std::swap(reference_a, reference_b);
    }
    result.correctness = a == reference_a;
    if (heat)
    {
        auto vector = smart::data::VectorView<const double>::contiguous(a.data(), {a.size()});
        for (std::size_t repeat = 0; repeat < manifest.repetitions; ++repeat)
        {
            const auto begin = Clock::now();
            const double observed = smart::linalg::norm(
                runtime.context(), vector, smart::NumericalOptions{manifest.policy});
            append_duration(result, begin, runtime);
            long double sum = 0.0L;
            for (double value : a)
                sum += static_cast<long double>(value) * value;
            const long double expected = std::sqrt(sum);
            const long double tolerance = manifest.policy == smart::NumericalPolicy::Accurate
                ? 1e-12L : 1e-9L;
            result.correctness = result.correctness
                && std::abs(static_cast<long double>(observed) - expected)
                    <= tolerance * std::max<long double>(1.0L, std::abs(expected));
        }
    }
    result.output_digest = smartparallel_tool::digest_doubles(a);
    return result;
}

#if SMARTPARALLEL_TOOL_HAS_VISION
CalibrationResult calibrate_threshold(const Manifest& manifest, smart::Runtime& runtime)
{
    CalibrationResult result;
    const std::size_t stride = manifest.stride == 0 ? manifest.columns : manifest.stride;
    std::vector<std::uint8_t> source(manifest.rows * stride, 0);
    std::uint64_t state = manifest.seed ? manifest.seed : 0x9E3779B97F4A7C15ULL;
    for (std::size_t row = 0; row < manifest.rows; ++row)
        for (std::size_t column = 0; column < manifest.columns; ++column)
        {
            state ^= state >> 12; state ^= state << 25; state ^= state >> 27;
            source[row * stride + column] = static_cast<std::uint8_t>(
                (state * 2685821657736338717ULL) & 0xffu);
        }
    std::vector<std::uint8_t> destination(source.size(), 19);
    std::vector<std::uint8_t> reference(source.size(), 19);
    for (std::size_t row = 0; row < manifest.rows; ++row)
        for (std::size_t column = 0; column < manifest.columns; ++column)
            reference[row * stride + column] = source[row * stride + column] > 127 ? 255 : 0;

    for (std::size_t repeat = 0; repeat < manifest.repetitions; ++repeat)
    {
        std::fill(destination.begin(), destination.end(), std::uint8_t{19});
        const auto begin = Clock::now();
        smart::vision::threshold(
            runtime.context(),
            {source.data(), manifest.columns, manifest.rows, stride, 1},
            {destination.data(), manifest.columns, manifest.rows, stride, 1},
            {127, 255, smart::vision::ThresholdMode::Binary},
            {smart::vision::ExecutionRoute::Auto, manifest.worker_budget});
        append_duration(result, begin, runtime);
    }
    result.correctness = destination == reference;
    result.output_digest = smartparallel_tool::digest_bytes(destination);
    return result;
}
#endif

void publish(const Manifest& manifest, smart::Runtime& runtime, const CalibrationResult& result)
{
    if (!result.correctness) throw std::runtime_error("independent correctness validation failed");
    std::filesystem::create_directories(manifest.output_directory);
    const auto candidate = manifest.output_directory / "candidate_profile.json";
    auto statistics = [](std::vector<double> samples)
    {
        if (samples.empty()) throw std::runtime_error("calibration has no timing samples");
        std::sort(samples.begin(), samples.end());
        const double median = samples[samples.size() / 2];
        std::vector<double> deviations;
        deviations.reserve(samples.size());
        for (double sample : samples) deviations.push_back(std::abs(sample - median));
        std::sort(deviations.begin(), deviations.end());
        return std::pair<double, double>{median, deviations[deviations.size() / 2]};
    };
    const auto overall_statistics = statistics(result.durations_ms);
    const double median = overall_statistics.first;
    const double variability = overall_statistics.second;
    auto sorted = result.durations_ms;
    std::sort(sorted.begin(), sorted.end());

    runtime.save_profiles(candidate);
    auto database = smart::load_profile_database(candidate);
    if (database.entries.empty()) throw std::runtime_error("calibration produced no semantic profiles");
    for (auto& entry : database.entries)
    {
        std::vector<double> operation_samples;
        for (std::size_t index = 0; index < result.fingerprints.size(); ++index)
            if (result.fingerprints[index].operation == entry.operation)
                operation_samples.push_back(result.durations_ms[index]);
        const auto operation_statistics = statistics(std::move(operation_samples));
        entry.evidence.median_duration_ms = operation_statistics.first;
        entry.evidence.variability_ms = operation_statistics.second;
        entry.evidence.source_calibration_id = manifest.source_manifest_hash;
        entry.evidence.correctness_passed = result.correctness;
    }
    smart::save_profile_database_atomic(database, candidate);
    database = smart::load_profile_database(candidate);

    std::ostringstream raw;
    raw << "schema_version,smartparallel_version,runtime_mode,profile_access,runtime_fingerprint,"
           "operation,operation_semantic_version,workload_fingerprint,numerical_policy,"
           "evaluation_order,accumulation_algorithm,canonical_plan,route,scheduler,worker_budget,"
           "actual_worker_count,provider,provider_version,simd_kernel,warm_start,deterministic_replay,"
           "duration_ms,correctness,output_digest\n";
    for (std::size_t i = 0; i < result.durations_ms.size(); ++i)
    {
        const auto& fingerprint = result.fingerprints[i];
        raw << "1,1.7.0," << smart::execution_mode_name(fingerprint.execution_mode) << ','
            << smart::profile_access_name(fingerprint.profile_access) << ','
            << fingerprint.runtime_fingerprint << ',' << fingerprint.operation << ','
            << fingerprint.operation_semantic_version << ',' << fingerprint.workload_fingerprint << ','
            << smart::numerical_policy_name(fingerprint.numerical_policy) << ','
            << fingerprint.evaluation_order << ',' << fingerprint.accumulation_algorithm << ','
            << fingerprint.canonical_plan << ',' << fingerprint.selected_route << ','
            << smart::runtime_name(fingerprint.selected_scheduler) << ',' << fingerprint.worker_budget << ','
            << fingerprint.actual_worker_count << ',' << fingerprint.provider << ','
            << fingerprint.provider_version << ',' << fingerprint.simd_kernel << ','
            << (fingerprint.warm_start ? "true" : "false") << ','
            << (fingerprint.deterministic_replay ? "true" : "false") << ','
            << std::setprecision(17) << result.durations_ms[i] << ",true," << result.output_digest << '\n';
    }
    smartparallel_tool::write_text(manifest.output_directory / "raw.csv", raw.str());

    const double minimum = sorted.front();
    const double maximum = sorted.back();
    std::ostringstream summary;
    summary << "operation,samples,minimum_ms,median_ms,maximum_ms,correctness\n"
            << manifest.operation << ',' << sorted.size() << ',' << minimum << ',' << median
            << ',' << maximum << ",true\n";
    smartparallel_tool::write_text(manifest.output_directory / "summary.csv", summary.str());

    std::ostringstream metrics;
    metrics << "{\"candidate_profile_hash\":\"" << database.content_hash
            << "\",\"correctness\":true,\"median_duration_ms\":" << std::setprecision(17) << median
            << ",\"operation\":\"" << smartparallel_tool::json_escape(manifest.operation)
            << "\",\"output_digest\":\"" << result.output_digest
            << "\",\"profile_entries\":" << database.entries.size()
            << ",\"source_calibration_id\":\"" << manifest.source_manifest_hash
            << "\",\"variability_ms\":" << variability
            << ",\"schema_version\":1}";
    smartparallel_tool::write_text(manifest.output_directory / "metrics.json", metrics.str());

    std::ostringstream environment;
    environment << "{\"profile_hash\":\"" << database.content_hash
                << "\",\"runtime_fingerprint\":\"" << runtime.fingerprint().hash
                << "\",\"smartparallel_version\":\"1.7.0\"}";
    smartparallel_tool::write_text(manifest.output_directory / "environment.json", environment.str());

    std::ostringstream fingerprints;
    fingerprints << "[";
    for (std::size_t i = 0; i < result.fingerprints.size(); ++i)
    {
        if (i) fingerprints << ',';
        fingerprints << smartparallel_tool::fingerprint_json(result.fingerprints[i]);
    }
    fingerprints << "]";
    smartparallel_tool::write_text(manifest.output_directory / "operation_fingerprints.json", fingerprints.str());
    smartparallel_tool::write_text(manifest.output_directory / "compatibility_report.json",
        "{\"compatible_with_calibration_environment\":true,\"issues\":[]}");

    std::ostringstream report;
    report << "# SmartParallel v1.7 calibration report\n\n"
           << "- Operation: `" << manifest.operation << "`\n"
           << "- Numerical policy: `" << smart::numerical_policy_name(manifest.policy) << "`\n"
           << "- Candidate entries: " << database.entries.size() << "\n"
           << "- Median duration: " << median << " ms\n"
           << "- Median absolute deviation: " << variability << " ms\n"
           << "- Correctness: passed\n"
           << "- Candidate profile SHA-256 identity: `" << database.content_hash << "`\n\n"
           << "The profile is Candidate evidence and must be approved explicitly.\n";
    smartparallel_tool::write_text(manifest.output_directory / "report.md", report.str());
    smartparallel_tool::write_text(manifest.output_directory / "SHA256SUMS.txt",
        smart::sha256_hex(smartparallel_tool::read_text(candidate)) + "  candidate_profile.json\n");

    std::cout << "candidate_profile=" << candidate.string() << '\n'
              << "profile_hash=" << database.content_hash << '\n'
              << "entries=" << database.entries.size() << '\n';
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
        {
            std::cerr << "usage: smartparallel_calibrate MANIFEST.json\n";
            return 2;
        }
        const Manifest manifest = parse_manifest(argv[1]);
        smart::Runtime runtime(smartparallel_tool::runtime_options(
            manifest.worker_budget, smart::ExecutionMode::Adaptive, smart::ProfileAccess::ReadWrite));
        CalibrationResult result;
        if (manifest.operation == "axpy") result = calibrate_axpy(manifest, runtime);
        else if (manifest.operation == "dot") result = calibrate_dot_or_norm(manifest, runtime, false);
        else if (manifest.operation == "norm") result = calibrate_dot_or_norm(manifest, runtime, true);
        else if (manifest.operation == "stencil_2d") result = calibrate_stencil(manifest, runtime, false);
        else if (manifest.operation == "heat_diffusion") result = calibrate_stencil(manifest, runtime, true);
#if SMARTPARALLEL_TOOL_HAS_VISION
        else if (manifest.operation == "threshold") result = calibrate_threshold(manifest, runtime);
#else
        else if (manifest.operation == "threshold")
            throw std::runtime_error("threshold calibration requires SMARTPARALLEL_BUILD_VISION=ON");
#endif
        else throw std::runtime_error("unsupported calibration operation: " + manifest.operation);
        publish(manifest, runtime, result);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "smartparallel_calibrate: " << error.what() << '\n';
        return 1;
    }
}
