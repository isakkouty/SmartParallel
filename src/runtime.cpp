#include <smart/runtime/runtime.hpp>
#include <smart/runtime/detail/state.hpp>
#include <smart/profiling/function_profile_cache.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/decision/hierarchical_residual_model.hpp>
#include <smart/decision/exploration_policy.hpp>
#include <smart/decision/backend_calibration.hpp>
#include <smart/execution/algorithm_dispatch.hpp>
#include <smart/execution/runtime_capabilities.hpp>
#include <smart/hardware/hardware_characteristics.hpp>
#include <smart/version.hpp>

#include <cfenv>
#include <cfloat>
#include <climits>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#  include <intrin.h>
#elif (defined(__GNUC__) || defined(__clang__)) \
    && (defined(__x86_64__) || defined(__i386__))
#  include <cpuid.h>
#endif

namespace smart
{
namespace detail
{
struct RuntimeAdaptiveState
{
    FunctionProfileCache function_profiles;
    ExperienceDatabase experience;
    HierarchicalResidualLearner residual;
    OnlineExplorationPolicy exploration;
    BackendCalibrationCache backend_calibration;
    AlgorithmDispatchCache algorithm_dispatch;
};

thread_local RuntimeAdaptiveState* active_runtime_adaptive_state = nullptr;

void* bind_runtime_adaptive_state(
    const std::shared_ptr<void>& runtime_state,
    bool use_legacy_global_state) noexcept
{
    RuntimeAdaptiveState* previous = active_runtime_adaptive_state;
    if (use_legacy_global_state || !runtime_state)
    {
        active_runtime_adaptive_state = nullptr;
    }
    else
    {
        const auto state = std::static_pointer_cast<RuntimeState>(runtime_state);
        active_runtime_adaptive_state =
            static_cast<RuntimeAdaptiveState*>(state->adaptive_state.get());
    }
    return previous;
}

void restore_runtime_adaptive_state(void* previous) noexcept
{
    active_runtime_adaptive_state = static_cast<RuntimeAdaptiveState*>(previous);
}

FunctionProfileCache* active_runtime_function_profile_cache() noexcept
{
    return active_runtime_adaptive_state == nullptr
        ? nullptr : &active_runtime_adaptive_state->function_profiles;
}
ExperienceDatabase* active_runtime_experience_database() noexcept
{
    return active_runtime_adaptive_state == nullptr
        ? nullptr : &active_runtime_adaptive_state->experience;
}
HierarchicalResidualLearner* active_runtime_hierarchical_residual_learner() noexcept
{
    return active_runtime_adaptive_state == nullptr
        ? nullptr : &active_runtime_adaptive_state->residual;
}
OnlineExplorationPolicy* active_runtime_online_exploration_policy() noexcept
{
    return active_runtime_adaptive_state == nullptr
        ? nullptr : &active_runtime_adaptive_state->exploration;
}
BackendCalibrationCache* active_runtime_backend_calibration_cache() noexcept
{
    return active_runtime_adaptive_state == nullptr
        ? nullptr : &active_runtime_adaptive_state->backend_calibration;
}
AlgorithmDispatchCache* active_runtime_algorithm_dispatch_cache() noexcept
{
    return active_runtime_adaptive_state == nullptr
        ? nullptr : &active_runtime_adaptive_state->algorithm_dispatch;
}
} // namespace detail

namespace
{
std::string architecture_name()
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#else
    return "unknown";
#endif
}
std::string os_name()
{
#if defined(_WIN32)
    return "windows";
#elif defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "macos";
#else
    return "unknown";
#endif
}
std::string compiler_name()
{
#if defined(_MSC_VER)
    return "msvc";
#elif defined(__clang__)
    return "clang";
#elif defined(__GNUC__)
    return "gcc";
#else
    return "unknown";
#endif
}
std::string compiler_version()
{
#if defined(_MSC_VER)
    return std::to_string(_MSC_VER);
#elif defined(__clang__)
    return std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) + "." + std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
    return std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__);
#else
    return "unknown";
#endif
}
std::string standard_library_name()
{
#if defined(_LIBCPP_VERSION)
    return "libc++-" + std::to_string(_LIBCPP_VERSION);
#elif defined(__GLIBCXX__)
    return "libstdc++-" + std::to_string(__GLIBCXX__);
#elif defined(_MSVC_STL_VERSION)
    return "msvc-stl-" + std::to_string(_MSVC_STL_VERSION);
#else
    return "unknown";
#endif
}
std::string endianness_name()
{
    const std::uint16_t value = 1;
    return *reinterpret_cast<const std::uint8_t*>(&value) == 1 ? "little" : "big";
}
std::string cpu_identity(const HardwareCharacteristics& hardware)
{
    std::string vendor = "unknown";
    unsigned int family = 0;
    unsigned int model = 0;
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int registers[4]{};
    __cpuid(registers, 0);
    char vendor_chars[13]{};
    std::memcpy(vendor_chars + 0, &registers[1], 4);
    std::memcpy(vendor_chars + 4, &registers[3], 4);
    std::memcpy(vendor_chars + 8, &registers[2], 4);
    vendor = vendor_chars;
    __cpuid(registers, 1);
    const unsigned int eax = static_cast<unsigned int>(registers[0]);
    const unsigned int base_family = (eax >> 8U) & 0x0fU;
    const unsigned int base_model = (eax >> 4U) & 0x0fU;
    const unsigned int extended_family = (eax >> 20U) & 0xffU;
    const unsigned int extended_model = (eax >> 16U) & 0x0fU;
    family = base_family == 0x0fU ? base_family + extended_family : base_family;
    model = (base_family == 0x06U || base_family == 0x0fU)
        ? base_model + (extended_model << 4U) : base_model;
#elif (defined(__GNUC__) || defined(__clang__)) \
    && (defined(__x86_64__) || defined(__i386__))
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    const unsigned int maximum_leaf = __get_cpuid_max(0, nullptr);
    if (__get_cpuid(0, &eax, &ebx, &ecx, &edx))
    {
        char vendor_chars[13]{};
        std::memcpy(vendor_chars + 0, &ebx, 4);
        std::memcpy(vendor_chars + 4, &edx, 4);
        std::memcpy(vendor_chars + 8, &ecx, 4);
        vendor = vendor_chars;
    }
    if (maximum_leaf >= 1
        && __get_cpuid(1, &eax, &ebx, &ecx, &edx))
    {
        const unsigned int base_family = (eax >> 8U) & 0x0fU;
        const unsigned int base_model = (eax >> 4U) & 0x0fU;
        const unsigned int extended_family = (eax >> 20U) & 0xffU;
        const unsigned int extended_model = (eax >> 16U) & 0x0fU;
        family = base_family == 0x0fU ? base_family + extended_family : base_family;
        model = (base_family == 0x06U || base_family == 0x0fU)
            ? base_model + (extended_model << 4U) : base_model;
    }
#endif
    std::ostringstream out;
    out << "vendor=" << vendor
        << ";family=" << family
        << ";model=" << model
        << ";logical=" << hardware.logical_threads
        << ";physical=" << hardware.physical_cores
        << ";cacheline=" << hardware.cache_line_size;
    return out.str();
}

std::string floating_point_identity()
{
    std::ostringstream out;
    out << "round=" << std::fegetround()
        << ";float_iec559=" << (std::numeric_limits<float>::is_iec559 ? 1 : 0)
        << ";double_iec559=" << (std::numeric_limits<double>::is_iec559 ? 1 : 0)
        << ";flt_eval=" << FLT_EVAL_METHOD;
    return out.str();
}
std::string feature_identity()
{
    std::ostringstream out;
    out << "tbb=" << SMARTPARALLEL_HAS_TBB;
#if defined(__AVX2__)
    out << ";avx2=1";
#else
    out << ";avx2=0";
#endif
#if defined(__SSE2__) || defined(_M_X64)
    out << ";sse2=1";
#else
    out << ";sse2=0";
#endif
    return out.str();
}
ProfileEnvironment make_environment(const RuntimeOptions& options)
{
    ProfileEnvironment environment;
    environment.architecture = architecture_name();
    const auto hardware = hardware_characteristics();
    environment.cpu_identity = cpu_identity(hardware);
    environment.required_isa = "baseline";
    environment.pointer_width = sizeof(void*) * 8;
    environment.endianness = endianness_name();
    environment.os_family = os_name();
    environment.compiler_identity = compiler_name();
    environment.compiler_version = compiler_version();
    environment.standard_library_identity = standard_library_name();
    environment.build_type = options.build_type;
    environment.smartparallel_version = SMARTPARALLEL_VERSION_STRING;
    environment.feature_macros = feature_identity();
    environment.tbb_version = SMARTPARALLEL_HAS_TBB ? "enabled" : "disabled";
    environment.opencv_version = "not-snapshotted-by-core";
    environment.floating_point_environment = floating_point_identity();
    environment.application_build_identifier = options.application_build_identifier;
    const std::string build_identity = environment.architecture + "|" + environment.os_family
        + "|" + environment.compiler_identity + "|" + environment.compiler_version
        + "|" + environment.standard_library_identity + "|" + environment.build_type
        + "|" + environment.smartparallel_version + "|" + environment.feature_macros;
    environment.smartparallel_build_fingerprint = sha256_hex(build_identity);
    return environment;
}
std::string runtime_identity(const RuntimeOptions& options,
                             const ProfileEnvironment& environment,
                             const Config& config)
{
    std::ostringstream out;
    out << "smartparallel=" << SMARTPARALLEL_VERSION_STRING
        << ";mode=" << execution_mode_name(options.execution_mode)
        << ";profile_access=" << profile_access_name(options.profile_access)
        << ";worker_budget=" << options.worker_budget
        << ";effective_budget=" << config.nested_root_concurrency_budget
        << ";numerical_default=" << numerical_policy_name(options.default_numerical_policy)
        << ";engine=" << runtime_name(config.execution_engine)
        << ";thread_pool=1;static_thread=1;tbb=" << SMARTPARALLEL_HAS_TBB
        << ";architecture=" << environment.architecture
        << ";cpu=" << environment.cpu_identity
        << ";compiler=" << environment.compiler_identity << '-' << environment.compiler_version
        << ";stdlib=" << environment.standard_library_identity
        << ";build=" << environment.smartparallel_build_fingerprint
        << ";fenv=" << environment.floating_point_environment
        << ";app=" << environment.application_build_identifier;
    return out.str();
}
void validate_options(const RuntimeOptions& options)
{
    if (options.worker_budget > 0 && options.worker_budget > hardware_threads())
        throw std::invalid_argument("SmartParallel Runtime worker budget exceeds available logical threads");
    if (options.profile_access == ProfileAccess::ReadOnly && options.profile_path.empty())
        throw std::invalid_argument("SmartParallel ReadOnly Runtime requires a profile path");
    if (options.profile_access == ProfileAccess::Disabled && !options.profile_path.empty())
        throw std::invalid_argument("SmartParallel Disabled profile access cannot specify a profile path");
    if (options.execution_mode == ExecutionMode::Deterministic
        && options.profile_access == ProfileAccess::Disabled
        && options.scheduler_config.execution_engine == ExecutionEngineType::Auto)
        throw std::invalid_argument("SmartParallel Deterministic Runtime without profiles requires an explicit forced scheduler");
}
void disable_adaptive_maintenance(Config& config)
{
    config.enable_experience = false;
    config.enable_parallel_for_auto_profiling = false;
    config.enable_parallel_for_profile_cache = false;
    config.enable_parallel_for_backend_calibration = false;
    config.enable_parallel_algorithm_hot_dispatch = false;
    config.enable_vision_adaptive_routes = false;
    config.enable_utility_model_runtime = false;
    config.enable_experience_persistence = false;
    config.enable_experience_autosave = false;
    config.enable_predictive_shadow = false;
    config.enable_predictive_decisions = false;
    config.enable_machine_runtime_calibration = false;
    config.enable_adaptive_execution_candidates = false;
    config.enable_prediction_calibration = false;
    config.enable_experience_ranking = false;
    config.enable_similarity_transfer = false;
    config.enable_residual_correction = false;
    config.enable_online_exploration = false;
    config.vision_route_pause_maintenance = true;
}
}

const char* execution_mode_name(ExecutionMode mode) noexcept
{
    return mode == ExecutionMode::Deterministic ? "Deterministic" : "Adaptive";
}
const char* profile_access_name(ProfileAccess access) noexcept
{
    switch (access)
    {
        case ProfileAccess::Disabled: return "Disabled";
        case ProfileAccess::ReadOnly: return "ReadOnly";
        case ProfileAccess::ReadWrite: return "ReadWrite";
    }
    return "Unknown";
}

Runtime::Runtime(RuntimeOptions options)
    : state_(std::make_shared<detail::RuntimeState>())
{
    validate_options(options);
    Config configuration = options.scheduler_config;
    const std::size_t effective_budget = options.worker_budget == 0
        ? hardware_threads() : options.worker_budget;
    configuration.nested_root_concurrency_budget = effective_budget;
    if (options.execution_mode == ExecutionMode::Deterministic)
        disable_adaptive_maintenance(configuration);

    state_->options = std::move(options);
    state_->configuration = std::make_shared<const Config>(std::move(configuration));
    state_->adaptive_state = std::make_shared<detail::RuntimeAdaptiveState>();
    state_->environment = make_environment(state_->options);
    state_->runtime_fingerprint.canonical_identity = runtime_identity(
        state_->options, state_->environment, *state_->configuration);
    state_->runtime_fingerprint.hash = sha256_hex(state_->runtime_fingerprint.canonical_identity);

    state_->profiles.schema_version = 1;
    state_->profiles.semantic_version = "1.0";
    state_->profiles.smartparallel_version = SMARTPARALLEL_VERSION_STRING;
    state_->profiles.environment = state_->environment;

    if (state_->options.profile_access != ProfileAccess::Disabled
        && !state_->options.profile_path.empty())
    {
        state_->profiles = load_profile_database(state_->options.profile_path);
        state_->profiles_loaded_from_file = true;
        const auto integrity = validate_profile_database_integrity(state_->profiles);
        if (!integrity.compatible)
            throw std::runtime_error("SmartParallel Runtime rejected a profile database with invalid integrity");
    }
}

ExecutionContext Runtime::context() const noexcept
{
    ExecutionContext context;
    context.runtime_config = state_->configuration;
    context.runtime_state = state_;
    context.inherited_concurrency_budget = std::max<std::size_t>(
        std::size_t{1}, state_->configuration->nested_root_concurrency_budget);
    return context;
}
const RuntimeOptions& Runtime::options() const noexcept { return state_->options; }

void Runtime::load_profiles(const std::filesystem::path& path)
{
    if (state_->options.profile_access == ProfileAccess::Disabled)
        throw std::logic_error("SmartParallel profile loading is disabled for this Runtime");
    ProfileDatabase loaded = load_profile_database(path);
    std::lock_guard<std::mutex> lock(state_->profiles_mutex);
    state_->profiles = std::move(loaded);
    state_->warm_started_entries.clear();
    state_->profiles_loaded_from_file = true;
}

void Runtime::save_profiles(const std::filesystem::path& path) const
{
    if (state_->options.profile_access != ProfileAccess::ReadWrite)
        throw std::logic_error("SmartParallel profile saving requires ReadWrite access");
    ProfileDatabase snapshot;
    {
        std::lock_guard<std::mutex> lock(state_->profiles_mutex);
        snapshot = state_->profiles;
    }
    save_profile_database_atomic(snapshot, path);
}

ProfileDatabaseSnapshot Runtime::profiles() const
{
    std::lock_guard<std::mutex> lock(state_->profiles_mutex);
    return state_->profiles;
}
RuntimeFingerprint Runtime::fingerprint() const { return state_->runtime_fingerprint; }
RuntimeTelemetrySnapshot Runtime::telemetry() const noexcept
{
    RuntimeTelemetrySnapshot result;
#define SMART_COPY_COUNTER(name) result.name = state_->name.load(std::memory_order_relaxed)
    SMART_COPY_COUNTER(operation_calls); SMART_COPY_COUNTER(deterministic_replays);
    SMART_COPY_COUNTER(adaptive_warm_starts); SMART_COPY_COUNTER(adaptive_cold_starts);
    SMART_COPY_COUNTER(learning_samples); SMART_COPY_COUNTER(timing_probes);
    SMART_COPY_COUNTER(holdout_probes); SMART_COPY_COUNTER(drift_probes);
    SMART_COPY_COUNTER(route_switches); SMART_COPY_COUNTER(profile_mutations);
    SMART_COPY_COUNTER(profile_file_reads_after_construction);
    SMART_COPY_COUNTER(profile_file_writes_from_operations);
#undef SMART_COPY_COUNTER
    return result;
}
OperationExecutionFingerprint Runtime::last_operation_fingerprint() const
{
    std::lock_guard<std::mutex> lock(state_->fingerprint_mutex);
    return state_->last_operation;
}

Runtime& default_runtime()
{
    static Runtime runtime([]{
        RuntimeOptions options;
        options.scheduler_config = global_config();
        options.worker_budget = 0;
        return options;
    }());
    return runtime;
}

ExecutionContext default_execution_context()
{
    ExecutionContext context = default_runtime().context();
    // The process-default Runtime preserves the legacy mutable configuration
    // surface. Explicit Runtime instances retain immutable snapshots.
    context.runtime_config.reset();
    context.legacy_global_adaptive_state = true;
    context.inherited_concurrency_budget = std::max<std::size_t>(
        std::size_t{1}, global_config().nested_root_concurrency_budget);
    return context;
}

ExecutionContext implicit_execution_context()
{
    const ExecutionContext current = current_execution_context();
    if (current.depth > 0 || current.runtime_state)
        return current;
    return default_execution_context();
}

NumericalPolicy execution_context_default_numerical_policy(
    const ExecutionContext& context) noexcept
{
    if (!context.runtime_state)
        return NumericalPolicy::Fast;
    const auto state = std::static_pointer_cast<detail::RuntimeState>(
        context.runtime_state);
    return state->options.default_numerical_policy;
}
} // namespace smart
