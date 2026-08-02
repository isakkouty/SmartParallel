#include "tool_common.hpp"

namespace
{
struct ReplayOptions
{
    std::filesystem::path profile;
    std::filesystem::path output;
    std::size_t rows = 256;
    std::size_t columns = 256;
    std::size_t iterations = 20;
    std::size_t workers = 1;
    std::size_t stride = 0;
    std::uint64_t seed = 170;
    smart::NumericalPolicy policy = smart::NumericalPolicy::Reproducible;
};

ReplayOptions parse(int argc, char** argv)
{
    if (argc != 9 && argc != 10) throw std::runtime_error(
        "usage: smartparallel_replay run APPROVED.json MANIFEST.json ROWS COLUMNS ITERATIONS WORKERS SEED [ROW_STRIDE]");
    ReplayOptions result;
    result.profile = argv[2]; result.output = argv[3];
    result.rows = static_cast<std::size_t>(std::stoull(argv[4]));
    result.columns = static_cast<std::size_t>(std::stoull(argv[5]));
    result.iterations = static_cast<std::size_t>(std::stoull(argv[6]));
    result.workers = static_cast<std::size_t>(std::stoull(argv[7]));
    result.seed = static_cast<std::uint64_t>(std::stoull(argv[8]));
    result.stride = argc == 10 ? static_cast<std::size_t>(std::stoull(argv[9]))
                               : result.columns;
    if (result.stride < result.columns)
        throw std::runtime_error("ROW_STRIDE must be at least COLUMNS");
    return result;
}

int run(const ReplayOptions& options)
{
    smart::Runtime runtime(smartparallel_tool::runtime_options(
        options.workers, smart::ExecutionMode::Deterministic,
        smart::ProfileAccess::ReadOnly, options.profile));
    const auto database = runtime.profiles();
    std::vector<double> a = smartparallel_tool::deterministic_values(
        options.rows * options.stride, options.seed);
    const std::string input_digest = smartparallel_tool::digest_doubles(a);
    std::vector<double> b(a.size(), 0.0);
    smart::scientific::Stencil2DCoefficients<double> coefficients;
    coefficients.center = 0.5;
    coefficients.north = coefficients.south = coefficients.west = coefficients.east = 0.125;
    std::vector<smart::OperationExecutionFingerprint> fingerprints;
    for (std::size_t iteration = 0; iteration < options.iterations; ++iteration)
    {
        auto input = smart::data::MatrixView<const double>(
            a.data(), {options.rows, options.columns}, {options.stride, 1});
        auto output = smart::data::MatrixView<double>(
            b.data(), {options.rows, options.columns}, {options.stride, 1});
        smart::scientific::stencil_2d(runtime.context(), input, output, coefficients,
                                      smart::NumericalOptions{options.policy});
        const auto fingerprint = runtime.last_operation_fingerprint();
        if (fingerprints.empty() || fingerprints.back().hash != fingerprint.hash)
            fingerprints.push_back(fingerprint);
        std::swap(a, b);
    }
    auto vector = smart::data::VectorView<const double>::contiguous(a.data(), {a.size()});
    const double final_norm = smart::linalg::norm(runtime.context(), vector,
                                                   smart::NumericalOptions{options.policy});
    const auto norm_fingerprint = runtime.last_operation_fingerprint();
    if (fingerprints.empty() || fingerprints.back().hash != norm_fingerprint.hash)
        fingerprints.push_back(norm_fingerprint);
    const auto telemetry = runtime.telemetry();
    if (telemetry.learning_samples != 0 || telemetry.timing_probes != 0
        || telemetry.holdout_probes != 0 || telemetry.drift_probes != 0
        || telemetry.route_switches != 0 || telemetry.profile_mutations != 0)
        throw std::runtime_error("deterministic replay performed adaptive maintenance");

    std::ostringstream operations;
    operations << '[';
    for (std::size_t i = 0; i < fingerprints.size(); ++i)
    {
        if (i) operations << ',';
        operations << smartparallel_tool::fingerprint_json(fingerprints[i]);
    }
    operations << ']';
    const std::string output_digest = smartparallel_tool::digest_doubles(a);
    std::ostringstream stable;
    stable << "{\"application_name\":\"SmartParallel heat diffusion pilot\""
           << ",\"application_version\":\"1.8.0\""
           << ",\"boundary_conditions\":\"copy_boundary_v1\""
           << ",\"columns\":" << options.columns
           << ",\"completion_status\":\"passed\""
           << ",\"final_norm_bits\":\"" << smart::sha256_hex(std::string(reinterpret_cast<const char*>(&final_norm), sizeof(final_norm))) << "\""
           << ",\"input_digest\":\"" << input_digest << "\""
           << ",\"iteration_count\":" << options.iterations
           << ",\"numerical_policy\":\"" << smart::numerical_policy_name(options.policy) << "\""
           << ",\"operation_fingerprints\":" << operations.str()
           << ",\"output_digest\":\"" << output_digest << "\""
           << ",\"profile_database_hash\":\"" << database.content_hash << "\""
           << ",\"rows\":" << options.rows
           << ",\"row_stride\":" << options.stride
           << ",\"runtime_fingerprint\":\"" << runtime.fingerprint().hash << "\""
           << ",\"resource_governor\":{"
           << "\"cpu_budget\":" << runtime.options().governor->cpu_budget()
           << ",\"fingerprint\":\"" << runtime.options().governor->fingerprint() << "\"}"
           << ",\"telemetry\":{" 
           << "\"adaptive_cold_starts\":" << telemetry.adaptive_cold_starts
           << ",\"adaptive_warm_starts\":" << telemetry.adaptive_warm_starts
           << ",\"deterministic_replays\":" << telemetry.deterministic_replays
           << ",\"drift_probes\":" << telemetry.drift_probes
           << ",\"holdout_probes\":" << telemetry.holdout_probes
           << ",\"learning_samples\":" << telemetry.learning_samples
           << ",\"profile_mutations\":" << telemetry.profile_mutations
           << ",\"route_switches\":" << telemetry.route_switches
           << ",\"timing_probes\":" << telemetry.timing_probes << "}"
           << ",\"schema_version\":2"
           << ",\"seed\":" << options.seed
           << ",\"worker_budget\":" << options.workers << '}';
    const std::string manifest = stable.str();
    smartparallel_tool::write_text(options.output, manifest);
    smartparallel_tool::write_text(options.output.string() + ".sha256",
                                    smart::sha256_hex(manifest) + "  " + options.output.filename().string() + "\n");
    std::cout << "manifest=" << options.output.string() << '\n'
              << "manifest_hash=" << smart::sha256_hex(manifest) << '\n'
              << "output_digest=" << output_digest << '\n';
    return 0;
}

int compare(const std::filesystem::path& a, const std::filesystem::path& b)
{
    const std::string left = smartparallel_tool::read_text(a);
    const std::string right = smartparallel_tool::read_text(b);
    const bool equal = left == right;
    std::cout << "manifest_equal=" << (equal ? "true" : "false")
              << " hash_a=" << smart::sha256_hex(left)
              << " hash_b=" << smart::sha256_hex(right) << '\n';
    return equal ? 0 : 1;
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc >= 2 && std::string(argv[1]) == "compare")
        {
            if (argc != 4) throw std::runtime_error("usage: smartparallel_replay compare MANIFEST_A MANIFEST_B");
            return compare(argv[2], argv[3]);
        }
        if (argc >= 2 && std::string(argv[1]) == "run") return run(parse(argc, argv));
        throw std::runtime_error("usage: smartparallel_replay run|compare ...");
    }
    catch (const std::exception& error)
    {
        std::cerr << "smartparallel_replay: " << error.what() << '\n';
        return 1;
    }
}
