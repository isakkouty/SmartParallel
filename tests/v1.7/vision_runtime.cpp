#include <smart/runtime/profile.hpp>
#include <smart/runtime/runtime.hpp>
#include <smart/runtime/detail/state.hpp>
#include <smart/vision/threshold.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

smart::RuntimeOptions options(std::size_t workers,
                              smart::ExecutionMode mode,
                              smart::ProfileAccess access,
                              const std::filesystem::path& path = {})
{
    smart::RuntimeOptions value;
    value.worker_budget = workers;
    value.execution_mode = mode;
    value.profile_access = access;
    value.profile_path = path;
    value.application_build_identifier = "v170-vision-validation";
    value.build_type = "Release";
    value.scheduler_config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    value.scheduler_config.enable_experience = false;
    value.scheduler_config.enable_vision_adaptive_routes = true;
    value.scheduler_config.vision_route_minimum_samples = 1;
    value.scheduler_config.vision_route_holdout_samples = 1;
    value.scheduler_config.vision_route_pause_maintenance = false;
    return value;
}

void execute(smart::Runtime& runtime, const std::vector<std::uint8_t>& source,
             std::vector<std::uint8_t>& destination,
             std::size_t width, std::size_t height, std::size_t stride)
{
    smart::vision::ImageView<const std::uint8_t> input{
        source.data(), width, height, stride, 1};
    smart::vision::ImageView<std::uint8_t> output{
        destination.data(), width, height, stride, 1};
    smart::vision::threshold(runtime.context(), input, output,
        smart::vision::ThresholdOptions{111, 255, smart::vision::ThresholdMode::Binary});
}
}

int main()
{
    try
    {
        const std::size_t workers = std::min<std::size_t>(2,
            std::max<std::size_t>(1, std::thread::hardware_concurrency()));
        const auto root = std::filesystem::temp_directory_path() / "smartparallel_v170_vision";
        std::error_code error; std::filesystem::remove_all(root, error);
        std::filesystem::create_directories(root);
        const auto candidate = root / "candidate.json";
        const auto approved = root / "approved.json";

        const std::size_t width = 64, height = 48, stride = 72;
        std::vector<std::uint8_t> source(height * stride, 0), destination(height * stride, 19);
        for (std::size_t y = 0; y < height; ++y)
            for (std::size_t x = 0; x < width; ++x)
                source[y * stride + x] = static_cast<std::uint8_t>((x * 7 + y * 13) & 255u);

        smart::Runtime selector_a(options(workers, smart::ExecutionMode::Adaptive,
                                      smart::ProfileAccess::Disabled));
        smart::Runtime selector_b(options(workers, smart::ExecutionMode::Adaptive,
                                      smart::ProfileAccess::Disabled));
        std::vector<std::uint8_t> selector_output_a(height * stride, 0);
        std::vector<std::uint8_t> selector_output_b(height * stride, 0);
        execute(selector_a, source, selector_output_a, width, height, stride);
        execute(selector_b, source, selector_output_b, width, height, stride);
        const auto selector_state_a =
            std::static_pointer_cast<smart::detail::RuntimeState>(
                selector_a.context().runtime_state);
        const auto selector_state_b =
            std::static_pointer_cast<smart::detail::RuntimeState>(
                selector_b.context().runtime_state);
        require(selector_state_a->vision_adaptive_state != nullptr
                && selector_state_b->vision_adaptive_state != nullptr,
                "explicit Runtime did not own a Vision selector");
        require(selector_state_a->vision_adaptive_state.get()
                    != selector_state_b->vision_adaptive_state.get(),
                "Vision adaptive selector leaked across Runtime instances");

        smart::Runtime calibration(options(workers, smart::ExecutionMode::Adaptive,
                                           smart::ProfileAccess::ReadWrite));
        execute(calibration, source, destination, width, height, stride);
        execute(calibration, source, destination, width, height, stride);
        calibration.save_profiles(candidate);
        auto database = smart::load_profile_database(candidate);
        require(database.entries.size() == 1, "threshold Candidate was not produced");
        require(database.entries.front().operation == "smart.vision.threshold",
                "threshold profile semantic identity is wrong");
        require(database.entries.front().workload.layout == "strided",
                "threshold profile lost exact strided layout");
        require(database.entries.front().evidence.route_authenticated,
                "threshold route was not authenticated");
        database.entries.front().candidate_source_hash = database.entries.front().entry_hash;
        database.entries.front().status = smart::ProfileStatus::Approved;
        smart::save_profile_database_atomic(database, approved);

        smart::Runtime replay(options(workers, smart::ExecutionMode::Deterministic,
                                      smart::ProfileAccess::ReadOnly, approved));
        std::vector<std::uint8_t> deterministic(height * stride, 19);
        execute(replay, source, deterministic, width, height, stride);
        for (std::size_t y = 0; y < height; ++y)
            for (std::size_t x = 0; x < width; ++x)
            {
                const std::uint8_t expected = source[y * stride + x] > 111 ? 255 : 0;
                require(deterministic[y * stride + x] == expected,
                        "deterministic threshold result is incorrect");
            }
        require(replay.last_operation_fingerprint().deterministic_replay,
                "threshold deterministic fingerprint flag is missing");
        const auto telemetry = replay.telemetry();
        require(telemetry.learning_samples == 0 && telemetry.timing_probes == 0
                && telemetry.holdout_probes == 0 && telemetry.drift_probes == 0
                && telemetry.profile_mutations == 0,
                "deterministic threshold performed adaptive maintenance");

        std::vector<std::uint8_t> wrong(height * (stride + 1), 23);
        const auto unchanged = wrong;
        bool mismatch = false;
        try { execute(replay, source, wrong, width, height, stride + 1); }
        catch (const std::runtime_error&) { mismatch = true; }
        require(mismatch, "threshold stride mismatch was not rejected");
        require(wrong == unchanged, "threshold mismatch modified destination");

        std::filesystem::remove_all(root, error);
        std::cout << "SmartParallel v1.7 Vision Runtime validation passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SmartParallel v1.7 Vision Runtime validation failed: "
                  << error.what() << '\n';
        return 1;
    }
}
