#pragma once

#include <smart/data/view.hpp>
#include <smart/linalg/operations.hpp>
#include <smart/runtime/profile.hpp>
#include <smart/runtime/runtime.hpp>
#include <smart/scientific/stencil.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace smartparallel_tool
{
inline std::string read_text(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), {});
}

inline void write_text(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("could not create " + path.string());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("could not write " + path.string());
}

inline std::string json_escape(const std::string& value)
{
    std::ostringstream out;
    for (unsigned char c : value)
    {
        switch (c)
        {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) out << "\\u" << std::hex << std::setw(4)
                                  << std::setfill('0') << static_cast<unsigned>(c)
                                  << std::dec;
                else out << static_cast<char>(c);
        }
    }
    return out.str();
}

inline std::size_t locate_value(const std::string& json, const std::string& key)
{
    const std::string token = "\"" + key + "\"";
    const auto position = json.find(token);
    if (position == std::string::npos) throw std::runtime_error("manifest missing field '" + key + "'");
    const auto colon = json.find(':', position + token.size());
    if (colon == std::string::npos) throw std::runtime_error("manifest field '" + key + "' is malformed");
    auto value = colon + 1;
    while (value < json.size() && std::isspace(static_cast<unsigned char>(json[value]))) ++value;
    return value;
}

inline std::string json_string(const std::string& json, const std::string& key,
                               const std::string& fallback = {})
{
    const std::string token = "\"" + key + "\"";
    if (json.find(token) == std::string::npos)
    {
        if (!fallback.empty()) return fallback;
        throw std::runtime_error("manifest missing field '" + key + "'");
    }
    auto value = locate_value(json, key);
    if (value >= json.size() || json[value] != '"') throw std::runtime_error("manifest field '" + key + "' must be a string");
    ++value;
    std::string result;
    bool escaped = false;
    for (; value < json.size(); ++value)
    {
        const char c = json[value];
        if (escaped)
        {
            switch (c) { case 'n': result.push_back('\n'); break; case 'r': result.push_back('\r'); break;
                         case 't': result.push_back('\t'); break; default: result.push_back(c); break; }
            escaped = false;
        }
        else if (c == '\\') escaped = true;
        else if (c == '"') return result;
        else result.push_back(c);
    }
    throw std::runtime_error("manifest string field '" + key + "' is unterminated");
}

inline std::size_t json_size(const std::string& json, const std::string& key,
                             std::size_t fallback = std::numeric_limits<std::size_t>::max())
{
    const std::string token = "\"" + key + "\"";
    if (json.find(token) == std::string::npos)
    {
        if (fallback != std::numeric_limits<std::size_t>::max()) return fallback;
        throw std::runtime_error("manifest missing field '" + key + "'");
    }
    auto value = locate_value(json, key);
    if (value >= json.size() || !std::isdigit(static_cast<unsigned char>(json[value])))
        throw std::runtime_error("manifest field '" + key + "' must be an unsigned integer");
    std::size_t result = 0;
    while (value < json.size() && std::isdigit(static_cast<unsigned char>(json[value])))
    {
        const unsigned digit = static_cast<unsigned>(json[value] - '0');
        if (result > (std::numeric_limits<std::size_t>::max() - digit) / 10)
            throw std::runtime_error("manifest integer overflow in '" + key + "'");
        result = result * 10 + digit;
        ++value;
    }
    return result;
}

inline smart::NumericalPolicy parse_policy(const std::string& value)
{
    if (value == "Fast" || value == "fast") return smart::NumericalPolicy::Fast;
    if (value == "Reproducible" || value == "reproducible") return smart::NumericalPolicy::Reproducible;
    if (value == "Accurate" || value == "accurate") return smart::NumericalPolicy::Accurate;
    throw std::runtime_error("unsupported numerical policy: " + value);
}

inline smart::RuntimeOptions runtime_options(std::size_t workers,
                                             smart::ExecutionMode mode,
                                             smart::ProfileAccess access,
                                             const std::filesystem::path& profile = {})
{
    smart::RuntimeOptions options;
    options.execution_mode = mode;
    options.profile_access = access;
    options.profile_path = profile;
    options.worker_budget = workers;
    options.default_numerical_policy = smart::NumericalPolicy::Reproducible;
    options.application_build_identifier = "smartparallel-v180-tools";
    options.build_type = "Release";
    options.scheduler_config.execution_engine = smart::ExecutionEngineType::ThreadPool;
    options.scheduler_config.enable_experience = false;
    options.scheduler_config.enable_experience_ranking = false;
    options.scheduler_config.enable_online_exploration = false;
    options.scheduler_config.enable_parallel_for_backend_calibration = false;
    options.scheduler_config.enable_parallel_for_auto_profiling = false;
    options.scheduler_config.enable_parallel_for_profile_cache = false;
    options.scheduler_config.enable_parallel_algorithm_hot_dispatch = false;
    options.scheduler_config.nested_min_iterations_per_worker = 1;
    options.scheduler_config.nested_min_parallel_work_ms = 0.0;
    options.scheduler_config.parallel_for_estimated_overhead_ms = 0.0;
    options.scheduler_config.parallel_for_minimum_predicted_speedup = 0.0;
    options.scheduler_config.small_workload_iteration_threshold = 0;
    options.scheduler_config.cheap_workload_sequential_threshold = 0;
    return options;
}

inline std::vector<double> deterministic_values(std::size_t count, std::uint64_t seed)
{
    std::vector<double> result(count);
    std::uint64_t state = seed ? seed : 0x9E3779B97F4A7C15ULL;
    for (auto& value : result)
    {
        state ^= state >> 12; state ^= state << 25; state ^= state >> 27;
        const std::uint64_t bits = state * 2685821657736338717ULL;
        value = static_cast<double>(bits % 1000003ULL) / 1000003.0;
    }
    return result;
}

inline std::string digest_doubles(const std::vector<double>& values)
{
    return smart::sha256_hex(std::string(reinterpret_cast<const char*>(values.data()),
                                         values.size() * sizeof(double)));
}

inline std::string digest_bytes(const std::vector<std::uint8_t>& values)
{
    return smart::sha256_hex(std::string(reinterpret_cast<const char*>(values.data()),
                                         values.size()));
}

inline std::string fingerprint_json(const smart::OperationExecutionFingerprint& f)
{
    std::ostringstream out;
    out << "{\"accumulation_algorithm\":\"" << json_escape(f.accumulation_algorithm)
        << "\",\"requested_workers\":" << f.requested_workers
        << ",\"minimum_workers\":" << f.minimum_workers
        << ",\"preferred_workers\":" << f.preferred_workers
        << ",\"maximum_workers\":" << f.maximum_workers
        << ",\"granted_workers\":" << f.granted_workers
        << ",\"scheduler_concurrency_cap\":" << f.scheduler_concurrency_cap
        << ",\"exact_grant_required\":" << (f.exact_grant_required ? "true" : "false")
        << ",\"lease_wait_policy\":\"" << smart::lease_wait_policy_name(f.lease_wait_policy)
        << "\",\"nested_lease_mode\":\"" << smart::nested_lease_mode_name(f.nested_lease_mode)
        << "\",\"provider_control_scope\":\"" << smart::control_scope_name(f.provider_control_scope)
        << "\",\"provider_control_strength\":\"" << smart::control_strength_name(f.provider_control_strength)
        << "\",\"provider_serialized\":" << (f.provider_serialized ? "true" : "false")
        << ",\"resource_fingerprint\":\"" << f.resource_fingerprint
        << "\",\"canonical_plan\":\"" << json_escape(f.canonical_plan)
        << "\",\"deterministic_replay\":" << (f.deterministic_replay ? "true" : "false")
        << ",\"evaluation_order\":\"" << json_escape(f.evaluation_order)
        << "\",\"execution_fingerprint\":\"" << f.hash
        << "\",\"execution_mode\":\"" << smart::execution_mode_name(f.execution_mode)
        << "\",\"forced_route\":" << (f.forced_route ? "true" : "false")
        << ",\"numerical_policy\":\"" << smart::numerical_policy_name(f.numerical_policy)
        << "\",\"operation\":\"" << json_escape(f.operation)
        << "\",\"operation_semantic_version\":\"" << json_escape(f.operation_semantic_version)
        << "\",\"profile_access\":\"" << smart::profile_access_name(f.profile_access)
        << "\",\"profile_database_hash\":\"" << f.profile_database_hash
        << "\",\"profile_entry_hash\":\"" << f.profile_entry_hash
        << "\",\"profile_status\":\"" << smart::profile_status_name(f.profile_status)
        << "\",\"provider\":\"" << json_escape(f.provider)
        << "\",\"provider_version\":\"" << json_escape(f.provider_version)
        << "\",\"route\":\"" << json_escape(f.selected_route)
        << "\",\"runtime_fingerprint\":\"" << f.runtime_fingerprint
        << "\",\"scheduler\":\"" << smart::runtime_name(f.selected_scheduler)
        << "\",\"simd_kernel\":\"" << json_escape(f.simd_kernel)
        << "\",\"warm_start\":" << (f.warm_start ? "true" : "false")
        << ",\"worker_budget\":" << f.worker_budget
        << ",\"workload_fingerprint\":\"" << f.workload_fingerprint << "\"}";
    return out.str();
}
}
