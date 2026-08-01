#include <smart/data/view.hpp>
#include <smart/linalg/operations.hpp>
#include <smart/runtime/profile.hpp>
#include <smart/runtime/runtime.hpp>
#include <smart/version.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{
std::filesystem::path temporary_root()
{
    const auto token = std::to_string(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    auto root = std::filesystem::temp_directory_path()
        / ("smartparallel-installed-profile-consumer-" + token);
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void approve(const std::filesystem::path& candidate,
             const std::filesystem::path& approved)
{
    auto database = smart::load_profile_database(candidate);
    if (database.entries.size() < 2)
        throw std::runtime_error("expected AXPY and norm Candidate profiles");
    for (auto& entry : database.entries)
    {
        if (entry.status != smart::ProfileStatus::Candidate
            || entry.evidence.sample_count < 2
            || !entry.evidence.route_authenticated
            || !entry.evidence.numerical_capability_passed
            || !entry.evidence.correctness_passed)
            throw std::runtime_error("Candidate evidence is incomplete");
        entry.candidate_source_hash = entry.entry_hash;
        entry.status = smart::ProfileStatus::Approved;
    }
    smart::save_profile_database_atomic(database, approved);
}
}

int main()
{
    try
    {
        static_assert(SMARTPARALLEL_VERSION_MAJOR == 1, "unexpected major version");
        static_assert(SMARTPARALLEL_VERSION_MINOR == 7, "unexpected minor version");

        const auto root = temporary_root();
        const auto candidate = root / "candidate.json";
        const auto approved = root / "approved.json";
        const std::size_t workers = std::min<std::size_t>(
            2, std::max<std::size_t>(1, std::thread::hardware_concurrency()));

        smart::RuntimeOptions adaptive_options;
        adaptive_options.worker_budget = workers;
        adaptive_options.profile_access = smart::ProfileAccess::ReadWrite;
        adaptive_options.default_numerical_policy = smart::NumericalPolicy::Reproducible;
        adaptive_options.application_build_identifier = "installed-profile-consumer-v1";
        adaptive_options.build_type = "Release";
        smart::Runtime adaptive(adaptive_options);

        std::vector<double> x(4096), y(4096, 2.0);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<double>((i * 17) % 101) / 13.0;
        const auto x_view = smart::data::VectorView<const double>::contiguous(
            x.data(), {x.size()});
        for (int repetition = 0; repetition < 2; ++repetition)
        {
            auto y_view = smart::data::VectorView<double>::contiguous(
                y.data(), {y.size()});
            smart::linalg::axpy(adaptive.context(), y_view, 0.25, x_view,
                smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
            const double norm = smart::linalg::norm(adaptive.context(), x_view,
                smart::NumericalOptions{smart::NumericalPolicy::Accurate});
            if (!std::isfinite(norm)) throw std::runtime_error("norm is not finite");
        }
        adaptive.save_profiles(candidate);
        approve(candidate, approved);

        smart::RuntimeOptions deterministic_options = adaptive_options;
        deterministic_options.execution_mode = smart::ExecutionMode::Deterministic;
        deterministic_options.profile_access = smart::ProfileAccess::ReadOnly;
        deterministic_options.profile_path = approved;
        smart::Runtime replay(deterministic_options);

        std::vector<double> replay_y(4096, 2.0);
        auto replay_y_view = smart::data::VectorView<double>::contiguous(
            replay_y.data(), {replay_y.size()});
        smart::linalg::axpy(replay.context(), replay_y_view, 0.25, x_view,
            smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
        const double replay_norm = smart::linalg::norm(replay.context(), x_view,
            smart::NumericalOptions{smart::NumericalPolicy::Accurate});
        if (!std::isfinite(replay_norm)
            || !replay.last_operation_fingerprint().deterministic_replay)
            throw std::runtime_error("deterministic scientific replay failed");

        std::vector<double> wrong_x(4097, 1.0), wrong_y(4097, 7.0);
        const auto unchanged = wrong_y;
        bool rejected = false;
        try
        {
            auto wrong_x_view = smart::data::VectorView<const double>::contiguous(
                wrong_x.data(), {wrong_x.size()});
            auto wrong_y_view = smart::data::VectorView<double>::contiguous(
                wrong_y.data(), {wrong_y.size()});
            smart::linalg::axpy(replay.context(), wrong_y_view, 0.25, wrong_x_view,
                smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
        }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected || wrong_y != unchanged)
            throw std::runtime_error("incompatible workload was not rejected before mutation");

        std::filesystem::remove_all(root);
        std::cout << "SmartParallel " << SMARTPARALLEL_VERSION_STRING
                  << " installed profile consumer passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "installed profile consumer failed: " << error.what() << '\n';
        return 1;
    }
}
