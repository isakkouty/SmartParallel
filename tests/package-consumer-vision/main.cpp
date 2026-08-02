#include <smart/runtime/profile.hpp>
#include <smart/runtime/runtime.hpp>
#include <smart/version.hpp>
#include <smart/vision/data_view_adapter.hpp>
#include <smart/vision/vision.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{
std::filesystem::path temporary_root()
{
    auto root = std::filesystem::temp_directory_path()
        / ("smartparallel-installed-vision-consumer-"
           + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void approve(const std::filesystem::path& candidate,
             const std::filesystem::path& approved)
{
    auto database = smart::load_profile_database(candidate);
    if (database.entries.size() != 1)
        throw std::runtime_error("expected one threshold profile");
    auto& entry = database.entries.front();
    if (entry.status != smart::ProfileStatus::Candidate
        || entry.evidence.sample_count < 2
        || !entry.evidence.route_authenticated
        || !entry.evidence.correctness_passed)
        throw std::runtime_error("threshold Candidate evidence is incomplete");
    entry.candidate_source_hash = entry.entry_hash;
    entry.status = smart::ProfileStatus::Approved;
    smart::save_profile_database_atomic(database, approved);
}
}

int main()
{
    try
    {
        static_assert(SMARTPARALLEL_VERSION_MINOR == 8, "unexpected version");
        const auto root = temporary_root();
        const auto candidate = root / "candidate.json";
        const auto approved = root / "approved.json";

        constexpr std::size_t width = 8;
        constexpr std::size_t height = 3;
        constexpr std::size_t stride = 11;
        std::vector<std::uint8_t> source(stride * height, 0);
        for (std::size_t row = 0; row < height; ++row)
            for (std::size_t column = 0; column < width; ++column)
                source[row * stride + column] = static_cast<std::uint8_t>(row * 60 + column * 20);

        smart::RuntimeOptions adaptive_options;
        adaptive_options.worker_budget = 1;
        adaptive_options.profile_access = smart::ProfileAccess::ReadWrite;
        adaptive_options.application_build_identifier = "installed-vision-consumer-v1";
        adaptive_options.build_type = "Release";
        smart::Runtime adaptive(adaptive_options);

        std::vector<std::uint8_t> destination(stride * height, 7);
        for (int repetition = 0; repetition < 2; ++repetition)
        {
            std::fill(destination.begin(), destination.end(), 7);
            smart::vision::threshold(
                adaptive.context(),
                {source.data(), width, height, stride},
                {destination.data(), width, height, stride},
                {127, 255, smart::vision::ThresholdMode::Binary},
                {smart::vision::ExecutionRoute::Auto, 1});
        }
        adaptive.save_profiles(candidate);
        approve(candidate, approved);

        smart::RuntimeOptions deterministic_options = adaptive_options;
        deterministic_options.execution_mode = smart::ExecutionMode::Deterministic;
        deterministic_options.profile_access = smart::ProfileAccess::ReadOnly;
        deterministic_options.profile_path = approved;
        smart::Runtime replay(deterministic_options);
        std::fill(destination.begin(), destination.end(), 9);
        smart::vision::threshold(
            replay.context(),
            {source.data(), width, height, stride},
            {destination.data(), width, height, stride},
            {127, 255, smart::vision::ThresholdMode::Binary});
        for (std::size_t row = 0; row < height; ++row)
            for (std::size_t column = 0; column < width; ++column)
            {
                const auto expected = source[row * stride + column] > 127 ? 255 : 0;
                if (destination[row * stride + column] != expected)
                    throw std::runtime_error("threshold replay result mismatch");
            }
        if (!replay.last_operation_fingerprint().deterministic_replay
            || replay.last_operation_fingerprint().provider != "native")
            throw std::runtime_error("threshold deterministic fingerprint mismatch");

        std::vector<std::uint8_t> mismatched(stride * height, 33);
        const auto unchanged = mismatched;
        bool rejected = false;
        try
        {
            smart::vision::threshold(
                replay.context(),
                {source.data(), width - 1, height, stride},
                {mismatched.data(), width - 1, height, stride},
                {127, 255, smart::vision::ThresholdMode::Binary});
        }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected || mismatched != unchanged)
            throw std::runtime_error("Vision mismatch did not fail before mutation");

        std::filesystem::remove_all(root);
        std::cout << "SmartParallel " << SMARTPARALLEL_VERSION_STRING
                  << " installed Vision profile consumer passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "installed Vision consumer failed: " << error.what() << '\n';
        return 1;
    }
}
