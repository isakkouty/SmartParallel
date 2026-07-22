#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <smart/core/config.hpp>
#include <smart/decision/decision_report.hpp>
#include <smart/decision/backend_calibration.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/execution/executor.hpp>
#include <smart/execution/nested_execution_coordinator.hpp>
#include <smart/execution/nested_execution_session.hpp>
#include <smart/execution/parallel.hpp>
#include <smart/execution/runtime_capabilities.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/decision/exploration_policy.hpp>
#include <smart/hardware/hardware.hpp>
#include <smart/profiling/function_profile_cache.hpp>
#include <smart/version.hpp>
#include <smart/workload/workload_builder.hpp>

#if SMARTPARALLEL_HAS_TBB && defined(__has_include)
#  if __has_include(<oneapi/tbb/version.h>)
#    include <oneapi/tbb/version.h>
#  endif
#endif

#ifdef _WIN32
#  define NOMINMAX
#  include <windows.h>
#  include <psapi.h>
#elif defined(__linux__) || defined(__APPLE__)
#  include <sys/resource.h>
#  include <sys/utsname.h>
#endif

namespace smart::real_world
{
using Clock = std::chrono::steady_clock;

inline std::string csv_escape(std::string value)
{
    bool quote = false;
    for (char c : value)
        quote = quote || c == ',' || c == '"' || c == '\n' || c == '\r';
    if (!quote)
        return value;
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char c : value)
    {
        if (c == '"')
            escaped.push_back('"');
        escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
}

inline std::vector<std::string> split_csv(const std::string& text)
{
    std::vector<std::string> values;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ','))
    {
        if (!item.empty())
            values.push_back(item);
    }
    return values;
}

inline bool contains(const std::vector<std::string>& values, const std::string& value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

inline std::string utc_timestamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

inline std::string compiler_name()
{
#if defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
    return "Clang " + std::to_string(__clang_major__) + "."
           + std::to_string(__clang_minor__) + "." + std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
    return "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "."
           + std::to_string(__GNUC_PATCHLEVEL__);
#else
    return "unknown";
#endif
}

inline std::string one_tbb_version()
{
#if SMARTPARALLEL_HAS_TBB
#  if defined(TBB_VERSION_STRING)
    return TBB_VERSION_STRING;
#  elif defined(TBB_VERSION_MAJOR) && defined(TBB_VERSION_MINOR)
    return std::to_string(TBB_VERSION_MAJOR) + "." + std::to_string(TBB_VERSION_MINOR);
#  else
    return "available_unknown_version";
#  endif
#else
    return "unavailable";
#endif
}

inline std::string operating_system()
{
#ifdef _WIN32
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown";
#endif
}

inline std::string cpu_model()
{
#ifdef _WIN32
    const char* identifier = std::getenv("PROCESSOR_IDENTIFIER");
    return identifier == nullptr ? "unknown" : identifier;
#elif defined(__linux__)
    std::ifstream input("/proc/cpuinfo");
    std::string line;
    while (std::getline(input, line))
    {
        const std::string key = "model name";
        if (line.compare(0, key.size(), key) == 0)
        {
            const auto colon = line.find(':');
            if (colon != std::string::npos)
                return line.substr(colon + 2);
        }
    }
    return "unknown";
#else
    struct utsname info{};
    return uname(&info) == 0 ? info.machine : "unknown";
#endif
}

inline std::uint64_t peak_working_set_bytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters)))
        return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
    return 0;
#elif defined(__linux__) || defined(__APPLE__)
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return 0;
#  if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#  else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ull;
#  endif
#else
    return 0;
#endif
}

inline double process_cpu_time_seconds()
{
#ifdef _WIN32
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        return 0.0;
    ULARGE_INTEGER kernel_value{};
    ULARGE_INTEGER user_value{};
    kernel_value.LowPart = kernel.dwLowDateTime;
    kernel_value.HighPart = kernel.dwHighDateTime;
    user_value.LowPart = user.dwLowDateTime;
    user_value.HighPart = user.dwHighDateTime;
    return static_cast<double>(kernel_value.QuadPart + user_value.QuadPart) / 1.0e7;
#elif defined(__linux__) || defined(__APPLE__)
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return 0.0;
    const double user_seconds = static_cast<double>(usage.ru_utime.tv_sec)
        + static_cast<double>(usage.ru_utime.tv_usec) / 1.0e6;
    const double system_seconds = static_cast<double>(usage.ru_stime.tv_sec)
        + static_cast<double>(usage.ru_stime.tv_usec) / 1.0e6;
    return user_seconds + system_seconds;
#else
    return static_cast<double>(std::clock()) / static_cast<double>(CLOCKS_PER_SEC);
#endif
}

inline double percentile(std::vector<double> values, double fraction)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const double position = fraction * static_cast<double>(values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
    if (lower == upper)
        return values[lower];
    const double alpha = position - static_cast<double>(lower);
    return values[lower] * (1.0 - alpha) + values[upper] * alpha;
}

inline double mean(const std::vector<double>& values)
{
    return values.empty() ? 0.0
                          : std::accumulate(values.begin(), values.end(), 0.0)
                                / static_cast<double>(values.size());
}

inline double standard_deviation(const std::vector<double>& values)
{
    if (values.size() < 2)
        return 0.0;
    const double average = mean(values);
    double sum = 0.0;
    for (double value : values)
    {
        const double delta = value - average;
        sum += delta * delta;
    }
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
}

inline std::uint64_t hash_bytes(const void* data, std::size_t size,
                                std::uint64_t seed = 1469598103934665603ull)
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = seed;
    for (std::size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

inline std::uint64_t mix64(std::uint64_t value)
{
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31;
    return value;
}

class ConcurrencyProbe
{
  public:
    class Scope
    {
      public:
        explicit Scope(ConcurrencyProbe& owner) : owner_(&owner) { owner_->enter(); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&& other) noexcept : owner_(other.owner_) { other.owner_ = nullptr; }
        ~Scope() { if (owner_ != nullptr) owner_->leave(); }
      private:
        ConcurrencyProbe* owner_ = nullptr;
    };

    Scope scope() { return Scope(*this); }
    void reset() noexcept
    {
        current_.store(0, std::memory_order_relaxed);
        maximum_.store(0, std::memory_order_relaxed);
    }
    std::size_t maximum() const noexcept
    {
        return maximum_.load(std::memory_order_relaxed);
    }

  private:
    using LocalEntry = std::pair<const ConcurrencyProbe*, std::size_t>;

    static std::vector<LocalEntry>& local_entries()
    {
        static thread_local std::vector<LocalEntry> entries;
        return entries;
    }

    void enter() noexcept
    {
        auto& entries = local_entries();
        const auto found = std::find_if(entries.begin(), entries.end(),
            [&](const LocalEntry& entry) { return entry.first == this; });
        if (found != entries.end())
        {
            ++found->second;
            return;
        }
        try
        {
            entries.emplace_back(this, 1);
        }
        catch (...)
        {
            // Diagnostic instrumentation must never change benchmark execution.
            return;
        }
        const std::size_t now = current_.fetch_add(1, std::memory_order_acq_rel) + 1;
        std::size_t maximum = maximum_.load(std::memory_order_relaxed);
        while (maximum < now && !maximum_.compare_exchange_weak(
                   maximum, now, std::memory_order_relaxed, std::memory_order_relaxed))
        {
        }
    }

    void leave() noexcept
    {
        auto& entries = local_entries();
        const auto found = std::find_if(entries.begin(), entries.end(),
            [&](const LocalEntry& entry) { return entry.first == this; });
        if (found == entries.end())
            return;
        if (--found->second != 0)
            return;
        entries.erase(found);
        current_.fetch_sub(1, std::memory_order_acq_rel);
    }

    std::atomic<std::size_t> current_{0};
    std::atomic<std::size_t> maximum_{0};
};

class BackendUsageProbe
{
  public:
    void reset() noexcept { mask_.store(0, std::memory_order_relaxed); }

    void record(ExecutionEngineType backend) noexcept
    {
        unsigned bit = 0;
        switch (backend)
        {
            case ExecutionEngineType::ThreadPool: bit = 1u; break;
            case ExecutionEngineType::StaticThread: bit = 2u; break;
            case ExecutionEngineType::OneTbb: bit = 4u; break;
            case ExecutionEngineType::Auto: break;
        }
        if (bit != 0)
            mask_.fetch_or(bit, std::memory_order_relaxed);
    }

    std::set<std::string> names() const
    {
        const unsigned mask = mask_.load(std::memory_order_relaxed);
        std::set<std::string> result;
        if ((mask & 1u) != 0) result.insert("thread_pool");
        if ((mask & 2u) != 0) result.insert("static_thread");
        if ((mask & 4u) != 0) result.insert("one_tbb");
        return result;
    }

  private:
    std::atomic<unsigned> mask_{0};
};

inline std::atomic<BackendUsageProbe*>& active_backend_usage_probe()
{
    static std::atomic<BackendUsageProbe*> probe{nullptr};
    return probe;
}

class ScopedBackendUsageObservation
{
  public:
    explicit ScopedBackendUsageObservation(BackendUsageProbe& probe)
        : previous_(active_backend_usage_probe().exchange(&probe, std::memory_order_acq_rel))
    {
    }
    ~ScopedBackendUsageObservation()
    {
        active_backend_usage_probe().store(previous_, std::memory_order_release);
    }
    ScopedBackendUsageObservation(const ScopedBackendUsageObservation&) = delete;
    ScopedBackendUsageObservation& operator=(const ScopedBackendUsageObservation&) = delete;

  private:
    BackendUsageProbe* previous_ = nullptr;
};

inline void record_backend_usage(ExecutionEngineType backend) noexcept
{
    if (auto* probe = active_backend_usage_probe().load(std::memory_order_acquire))
        probe->record(backend);
}

enum class ModeKind
{
    Sequential,
    ManualBackend,
    SmartAuto,
    SmartForcedSequential,
    SmartForcedBackend,
    OuterOnly,
    InnerOnly,
    AllLevels,
    Flattened
};

struct ModeSpec
{
    std::string name;
    ModeKind kind = ModeKind::Sequential;
    ExecutionEngineType backend = ExecutionEngineType::Auto;

    bool smart_mode() const noexcept
    {
        return kind == ModeKind::SmartAuto || kind == ModeKind::SmartForcedSequential
               || kind == ModeKind::SmartForcedBackend;
    }
};

inline ExecutionEngineType parse_backend(const std::string& value)
{
    if (value == "thread_pool" || value == "threadpool")
        return ExecutionEngineType::ThreadPool;
    if (value == "static_thread" || value == "static")
        return ExecutionEngineType::StaticThread;
    if (value == "one_tbb" || value == "tbb")
        return ExecutionEngineType::OneTbb;
    if (value == "auto")
        return ExecutionEngineType::Auto;
    throw std::invalid_argument("unknown backend: " + value);
}

inline std::string backend_name(ExecutionEngineType backend)
{
    return runtime_name(backend);
}

struct Options
{
    std::size_t repetitions = 15;
    std::size_t warmups = 3;
    std::size_t workers = std::max<std::size_t>(1, std::min<std::size_t>(4, hardware_threads()));
    std::uint64_t seed = 0x5A17BEEFull;
    std::filesystem::path output_directory = "validation/output/real_world";
    std::vector<std::string> presets{"all"};
    std::vector<std::string> modes{"all"};
    std::vector<std::string> backends{"all"};
    bool trace = false;
    bool list_presets = false;
    bool help = false;
};

inline void print_common_help(const char* executable, const std::vector<std::string>& presets)
{
    std::cout
        << "Usage: " << executable << " [options]\n"
        << "  --preset all|name[,name...]\n"
        << "  --mode all|core|sequential|manual|smart_auto|smart_forced_sequential|"
           "smart_forced|outer_only|inner_only|all_levels|flattened[,..]\n"
        << "  --backend all|thread_pool|static_thread|one_tbb[,..]\n"
        << "  --repetitions N       Timed warm repetitions (default 15)\n"
        << "  --warmups N           Untimed warm-ups (minimum recommended 3)\n"
        << "  --workers N           Root worker budget\n"
        << "  --seed N              Deterministic input seed\n"
        << "  --output-dir PATH     CSV output directory\n"
        << "  --trace               Export per-loop scheduler trace events\n"
        << "  --list-presets        Print workload presets\n"
        << "  --help\n\nPresets:\n";
    for (const auto& preset : presets)
        std::cout << "  " << preset << '\n';
}

inline Options parse_options(int argc, char** argv)
{
    Options options;
    auto require_value = [&](int& index) -> std::string
    {
        if (++index >= argc)
            throw std::invalid_argument(std::string("missing value after ") + argv[index - 1]);
        return argv[index];
    };
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument == "--repetitions")
            options.repetitions = static_cast<std::size_t>(std::stoull(require_value(i)));
        else if (argument == "--warmups")
            options.warmups = static_cast<std::size_t>(std::stoull(require_value(i)));
        else if (argument == "--workers")
            options.workers = static_cast<std::size_t>(std::stoull(require_value(i)));
        else if (argument == "--seed")
            options.seed = static_cast<std::uint64_t>(std::stoull(require_value(i), nullptr, 0));
        else if (argument == "--output-dir")
            options.output_directory = require_value(i);
        else if (argument == "--preset")
            options.presets = split_csv(require_value(i));
        else if (argument == "--mode")
            options.modes = split_csv(require_value(i));
        else if (argument == "--backend")
            options.backends = split_csv(require_value(i));
        else if (argument == "--trace")
            options.trace = true;
        else if (argument == "--list-presets")
            options.list_presets = true;
        else if (argument == "--help" || argument == "-h")
            options.help = true;
        else
            throw std::invalid_argument("unknown argument: " + argument);
    }
    options.repetitions = std::max<std::size_t>(1, options.repetitions);
    options.workers = std::max<std::size_t>(1, options.workers);
    return options;
}

inline std::vector<ExecutionEngineType> selected_backends(const Options& options)
{
    std::vector<ExecutionEngineType> result;
    const bool all = contains(options.backends, "all");
    const std::vector<ExecutionEngineType> candidates{
        ExecutionEngineType::ThreadPool,
        ExecutionEngineType::StaticThread,
        ExecutionEngineType::OneTbb};
    for (ExecutionEngineType candidate : candidates)
    {
        const std::string name = backend_name(candidate);
        if (!all && !contains(options.backends, name)
            && !(candidate == ExecutionEngineType::OneTbb && contains(options.backends, "tbb"))
            && !(candidate == ExecutionEngineType::StaticThread && contains(options.backends, "static")))
            continue;
        if (!execution_backend_available(candidate))
        {
            if (!all)
                throw std::runtime_error("requested backend is unavailable: " + name);
            std::cerr << "SKIP: optional backend unavailable: " << name << '\n';
            continue;
        }
        result.push_back(candidate);
    }
    return result;
}

inline bool mode_requested(const Options& options, const std::string& name)
{
    if (contains(options.modes, "all"))
        return true;
    if (contains(options.modes, name))
        return true;
    if (contains(options.modes, "core"))
        return name == "sequential" || name == "manual" || name == "smart_auto"
               || name == "smart_forced" || name == "outer_only"
               || name == "inner_only" || name == "all_levels" || name == "flattened";
    return false;
}

inline std::vector<ModeSpec> make_modes(const Options& options, bool nested)
{
    std::vector<ModeSpec> modes;
    if (mode_requested(options, "sequential"))
        modes.push_back({"sequential", ModeKind::Sequential, ExecutionEngineType::Auto});
    if (mode_requested(options, "smart_auto"))
        modes.push_back({nested ? "smart_auto_frontier" : "smart_auto",
                         ModeKind::SmartAuto,
                         ExecutionEngineType::Auto});
    if (mode_requested(options, "smart_forced_sequential"))
        modes.push_back({"smart_forced_sequential",
                         ModeKind::SmartForcedSequential,
                         ExecutionEngineType::Auto});

    for (ExecutionEngineType backend : selected_backends(options))
    {
        const std::string suffix = backend_name(backend);
        if (mode_requested(options, "manual"))
            modes.push_back({"manual_" + suffix, ModeKind::ManualBackend, backend});
        if (mode_requested(options, "smart_forced"))
            modes.push_back({"smart_forced_" + suffix, ModeKind::SmartForcedBackend, backend});
        if (nested && mode_requested(options, "outer_only"))
            modes.push_back({"outer_only_" + suffix, ModeKind::OuterOnly, backend});
        if (nested && mode_requested(options, "inner_only"))
            modes.push_back({"inner_only_" + suffix, ModeKind::InnerOnly, backend});
        if (nested && mode_requested(options, "all_levels"))
            modes.push_back({"all_levels_" + suffix, ModeKind::AllLevels, backend});
        if (nested && mode_requested(options, "flattened"))
            modes.push_back({"flattened_" + suffix, ModeKind::Flattened, backend});
    }
    if (modes.empty())
        throw std::runtime_error("no benchmark modes selected");
    return modes;
}

class ScopedConfig
{
  public:
    ScopedConfig(std::size_t workers, ExecutionEngineType engine, bool trace)
        : saved_(global_config())
    {
        auto& config = global_config();
        config.nested_root_concurrency_budget = std::max<std::size_t>(1, workers);
        config.execution_engine = engine;
        config.enable_nested_execution_trace = trace;
        config.enable_experience = false;
        config.enable_experience_persistence = false;
        config.enable_online_exploration = false;
        config.enable_parallel_for_backend_calibration = true;
        config.enable_timing_diagnostics = false;
    }

    ~ScopedConfig() { global_config() = saved_; }

    ScopedConfig(const ScopedConfig&) = delete;
    ScopedConfig& operator=(const ScopedConfig&) = delete;

  private:
    Config saved_;
};

inline void clear_runtime_learning()
{
    global_function_profile_cache().clear();
    global_experience_database().clear();
    global_online_exploration_policy().clear();
    global_backend_calibration_cache().clear();
    clear_nested_execution_trace();
}

inline ExecutionPlan fixed_plan(ExecutionEngineType backend,
                                std::size_t workers,
                                std::size_t total,
                                std::size_t chunk_size = 0)
{
    ExecutionPlan plan;
    if (total == 0 || workers <= 1)
    {
        plan.parallel = false;
        plan.strategy = ExecutionStrategy::Sequential;
        plan.engine = backend;
        plan.job_count = 1;
        return plan;
    }
    plan.parallel = true;
    plan.engine = backend;
    plan.strategy = backend == ExecutionEngineType::StaticThread
        ? ExecutionStrategy::StaticChunks
        : ExecutionStrategy::DynamicChunks;
    plan.job_count = std::max<std::size_t>(1, std::min(workers, total));
    plan.chunk_size = chunk_size;
    return plan;
}

template <typename Function>
void sequential_for(std::size_t count, Function&& function)
{
    for (std::size_t i = 0; i < count; ++i)
        function(i);
}

template <typename Function>
void fixed_parallel_for(std::size_t count,
                        ExecutionEngineType backend,
                        std::size_t workers,
                        Function&& function,
                        std::size_t chunk_size = 0)
{
    const Workload workload = WorkloadBuilder::index_range(count);
    const ExecutionPlan plan = fixed_plan(backend, workers, count, chunk_size);
    if (plan.parallel)
        record_backend_usage(resolve_execution_engine_type(plan.engine));
    execute_workload(workload, plan, std::forward<Function>(function));
}

template <typename Function>
void smart_parallel_for(std::size_t count,
                        ExecutionEngineType constrained_backend,
                        Function&& function)
{
    // Automatic execution needs no global configuration mutation. For a forced
    // backend, only the root wrapper establishes the process-wide constraint;
    // nested calls inherit it from the active execution context. This avoids
    // redundant writes and a data race between concurrent descendant callbacks.
    if (constrained_backend == ExecutionEngineType::Auto
        || current_execution_context().depth > 0
        || global_config().execution_engine == constrained_backend)
    {
        parallel_for(std::size_t{0}, count, std::forward<Function>(function));
        return;
    }

    const ExecutionEngineType saved = global_config().execution_engine;
    global_config().execution_engine = constrained_backend;
    try
    {
        parallel_for(std::size_t{0}, count, std::forward<Function>(function));
    }
    catch (...)
    {
        global_config().execution_engine = saved;
        throw;
    }
    global_config().execution_engine = saved;
}

template <typename Function>
void coordinated_parallel_for(std::size_t count,
                              ExecutionEngineType backend,
                              std::size_t workers,
                              Function&& function)
{
    ExecutionPlan requested = fixed_plan(backend, workers, count, 0);
    NestedExecutionCoordinator coordinator;
    auto decision = coordinator.coordinate(current_execution_context(), requested);
    NestedExecutionConstraints constraints;
    constraints.iteration_count = count;
    constraints.minimum_iterations_per_worker = 1;
    constraints.minimum_chunks_per_worker = 1;
    constraints.target_chunks_per_worker = 2;
    decision = coordinator.enforce_constraints(decision, constraints);
    if (decision.plan.parallel)
        record_backend_usage(resolve_execution_engine_type(decision.plan.engine));

    ExecutionContext child = detail::make_execution_context();
    child.engine = decision.plan.parallel ? resolve_execution_engine_type(decision.plan.engine)
                                          : ExecutionEngineType::Auto;
    child.parallel = decision.plan.parallel;
    child.nested_policy = decision.policy;
    child.concurrency_budget = decision.effective_budget;
    inherit_execution_lineage(child, current_execution_context());

    const Workload workload = WorkloadBuilder::index_range(count);
    detail::ExecutionContextScope backend_scope(child);
    execute_workload(workload,
                     decision.plan,
                     [&](std::size_t index)
                     {
                         detail::ExecutionContextScope callback_scope(child);
                         function(index);
                     },
                     decision.policy,
                     &child);
}

template <typename Function>
void with_fixed_root_session(ExecutionEngineType backend,
                             std::size_t workers,
                             Function&& function)
{
    ExecutionContext root;
    root.loop_id = detail::next_loop_id().fetch_add(1, std::memory_order_relaxed);
    root.depth = 1;
    root.engine = backend;
    root.parallel = true;
    root.concurrency_budget = std::max<std::size_t>(1, workers);
    root.nested_session =
        std::make_shared<NestedExecutionSession>(root.concurrency_budget, root.loop_id);
    inherit_execution_lineage(root, {});
    detail::ExecutionContextScope scope(root);
    function();
}

struct ValidationResult
{
    bool correct = false;
    std::uint64_t checksum = 0;
    std::uint64_t expected_checksum = 0;
    std::string message;
};

struct CaseDefinition
{
    std::string integration;
    std::string workload;
    std::string preset;
    std::string parameters;
    std::string unit_name = "items";
    double throughput_units = 0.0;
    std::size_t task_count = 0;
    std::function<void()> reset;
    std::function<void(const ModeSpec&)> execute;
    std::function<ValidationResult()> validate;
    std::function<std::size_t()> observed_concurrency;
};

struct DiagnosticInfo
{
    std::string actual_backend = "sequential";
    std::string selected_strategy = "sequential";
    std::string selected_frontier = "none";
    std::size_t max_concurrency = 1;
    std::size_t scheduler_decisions = 0;
    std::size_t cache_hits = 0;
    std::size_t stable_plan_reuse = 0;
    bool backend_confirmed = true;
};

struct RawSample
{
    std::string integration;
    std::string workload;
    std::string preset;
    std::string parameters;
    std::string mode;
    std::string requested_backend;
    std::string actual_backend;
    std::string selected_strategy;
    std::string selected_frontier;
    std::size_t repetition = 0;
    std::string state;
    double execution_ms = 0.0;
    double throughput_per_second = 0.0;
    double cpu_utilization_percent = 0.0;
    double process_cpu_equivalent_cores = 0.0;
    std::uint64_t peak_memory_bytes = 0;
    std::size_t task_count = 0;
    std::size_t max_concurrency = 1;
    std::size_t scheduler_decisions = 0;
    std::size_t cache_hits = 0;
    std::size_t stable_plan_reuse = 0;
    std::uint64_t checksum = 0;
    std::uint64_t expected_checksum = 0;
    bool correct = false;
    std::string exception_status = "none";
    std::string cancellation_status = "not_requested";
    std::size_t workers = 1;
    std::uint64_t seed = 0;
};

struct Summary
{
    std::string integration;
    std::string workload;
    std::string preset;
    std::string parameters;
    std::string mode;
    std::string requested_backend;
    std::string actual_backend;
    std::string selected_strategy;
    std::string selected_frontier;
    std::size_t repetitions = 0;
    std::size_t warmups = 0;
    double cold_ms = 0.0;
    double median_ms = 0.0;
    double mean_ms = 0.0;
    double minimum_ms = 0.0;
    double maximum_ms = 0.0;
    double standard_deviation_ms = 0.0;
    double p95_ms = 0.0;
    double p99_ms = 0.0;
    double throughput_per_second = 0.0;
    double speedup_over_sequential = 0.0;
    double absolute_regret_ms = 0.0;
    double percentage_regret = 0.0;
    double mean_cpu_utilization_percent = 0.0;
    double mean_process_cpu_equivalent_cores = 0.0;
    std::uint64_t peak_memory_bytes = 0;
    std::size_t task_count = 0;
    std::size_t max_concurrency = 1;
    std::size_t scheduler_decisions = 0;
    std::size_t cache_hits = 0;
    std::size_t stable_plan_reuse = 0;
    std::uint64_t checksum = 0;
    std::uint64_t expected_checksum = 0;
    bool correct = false;
    bool valid_for_ranking = false;
    std::string exception_status = "none";
    std::string cancellation_status = "not_requested";
    std::size_t workers = 1;
    std::uint64_t seed = 0;
    std::string unit_name;
};

struct TraceEnvelope
{
    std::string integration;
    std::string workload;
    std::string preset;
    std::string mode;
    NestedExecutionTraceRecord record;
};

inline DiagnosticInfo diagnose(const ModeSpec& mode,
                               const std::vector<NestedExecutionTraceRecord>& records,
                               const std::set<std::string>& explicitly_used_backends = {})
{
    DiagnosticInfo info;
    info.actual_backend = "sequential";

    std::set<std::size_t> parallel_depths;
    std::set<std::string> actual_backends = explicitly_used_backends;
    std::set<std::string> reasons;
    for (const auto& record : records)
    {
        ++info.scheduler_decisions;
        info.cache_hits += record.cache_hit ? 1u : 0u;
        if (record.plan_snapshot_hit || record.decision_reason == "stable_cached_plan")
            ++info.stable_plan_reuse;
        info.max_concurrency = std::max(
            info.max_concurrency,
            std::max(record.runtime_concurrency, record.max_root_leased_workers));
        if (record.parallel)
        {
            parallel_depths.insert(record.depth);
            if (record.backend_confirmed)
                actual_backends.insert(record.backend);
            else
                info.backend_confirmed = false;
        }
        if (!record.decision_reason.empty())
            reasons.insert(record.decision_reason);
    }
    if (!actual_backends.empty())
    {
        std::ostringstream joined;
        for (const auto& backend : actual_backends)
        {
            if (joined.tellp() > 0)
                joined << '+';
            joined << backend;
        }
        info.actual_backend = joined.str();
    }
    if (mode.kind == ModeKind::OuterOnly)
    {
        info.selected_frontier = "outer";
        info.selected_strategy = "outer_only";
    }
    else if (mode.kind == ModeKind::InnerOnly)
    {
        info.selected_frontier = "inner";
        info.selected_strategy = "inner_only";
    }
    else if (mode.kind == ModeKind::AllLevels)
    {
        info.selected_frontier = "all";
        info.selected_strategy = "all_levels";
    }
    else if (mode.kind == ModeKind::Flattened)
    {
        info.selected_frontier = "flattened";
        info.selected_strategy = "flattened";
    }
    else if (mode.kind == ModeKind::ManualBackend)
    {
        info.selected_frontier = "manual";
        info.selected_strategy = "fixed_plan";
    }
    else if (!parallel_depths.empty())
    {
        std::ostringstream frontier;
        for (std::size_t depth : parallel_depths)
        {
            if (frontier.tellp() > 0)
                frontier << '+';
            frontier << 'L' << depth;
        }
        info.selected_frontier = frontier.str();
        info.selected_strategy = "parallel";
    }
    else if (mode.kind == ModeKind::SmartForcedBackend)
    {
        info.selected_strategy = "adaptive_forced_backend";
    }
    return info;
}

inline RawSample measure_once(const CaseDefinition& definition,
                              const ModeSpec& mode,
                              const Options& options,
                              std::size_t repetition,
                              const std::string& state)
{
    definition.reset();
    const double cpu_start_seconds = process_cpu_time_seconds();
    const auto start = Clock::now();
    std::string exception_status = "none";
    try
    {
        definition.execute(mode);
    }
    catch (const std::exception& error)
    {
        exception_status = error.what();
    }
    catch (...)
    {
        exception_status = "unknown_exception";
    }
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    const double cpu_seconds = std::max(
        0.0, process_cpu_time_seconds() - cpu_start_seconds);
    ValidationResult validation;
    if (exception_status == "none")
        validation = definition.validate();
    else
    {
        validation.correct = false;
        validation.message = exception_status;
    }
    const double equivalent_cores = elapsed_ms > 0.0
        ? cpu_seconds / (elapsed_ms / 1000.0)
        : 0.0;
    const double utilization = std::max(0.0, equivalent_cores)
        / static_cast<double>(std::max<std::size_t>(1, hardware_threads())) * 100.0;

    RawSample sample;
    sample.integration = definition.integration;
    sample.workload = definition.workload;
    sample.preset = definition.preset;
    sample.parameters = definition.parameters;
    sample.mode = mode.name;
    sample.requested_backend = backend_name(mode.backend);
    sample.repetition = repetition;
    sample.state = state;
    sample.execution_ms = elapsed_ms;
    sample.throughput_per_second = elapsed_ms > 0.0
        ? definition.throughput_units / (elapsed_ms / 1000.0)
        : 0.0;
    sample.cpu_utilization_percent = utilization;
    sample.process_cpu_equivalent_cores = equivalent_cores;
    sample.peak_memory_bytes = peak_working_set_bytes();
    sample.task_count = definition.task_count;
    sample.checksum = validation.checksum;
    sample.expected_checksum = validation.expected_checksum;
    sample.correct = validation.correct && exception_status == "none";
    sample.exception_status = exception_status;
    sample.workers = options.workers;
    sample.seed = options.seed;
    return sample;
}

inline Summary summarize(const CaseDefinition& definition,
                         const ModeSpec& mode,
                         const Options& options,
                         const RawSample& cold,
                         const std::vector<RawSample>& warm,
                         const DiagnosticInfo& diagnostic)
{
    std::vector<double> timings;
    std::vector<double> cpu;
    std::vector<double> cpu_cores;
    timings.reserve(warm.size());
    cpu.reserve(warm.size());
    cpu_cores.reserve(warm.size());
    bool correct = cold.correct;
    std::uint64_t checksum = cold.checksum;
    std::uint64_t expected = cold.expected_checksum;
    std::uint64_t peak_memory = cold.peak_memory_bytes;
    for (const auto& sample : warm)
    {
        timings.push_back(sample.execution_ms);
        cpu.push_back(sample.cpu_utilization_percent);
        cpu_cores.push_back(sample.process_cpu_equivalent_cores);
        correct = correct && sample.correct;
        checksum = sample.checksum;
        expected = sample.expected_checksum;
        peak_memory = std::max(peak_memory, sample.peak_memory_bytes);
    }
    std::vector<double> sorted = timings;
    std::sort(sorted.begin(), sorted.end());

    Summary summary;
    summary.integration = definition.integration;
    summary.workload = definition.workload;
    summary.preset = definition.preset;
    summary.parameters = definition.parameters;
    summary.mode = mode.name;
    summary.requested_backend = backend_name(mode.backend);
    summary.actual_backend = diagnostic.actual_backend;
    summary.selected_strategy = diagnostic.selected_strategy;
    summary.selected_frontier = diagnostic.selected_frontier;
    summary.repetitions = warm.size();
    summary.warmups = options.warmups;
    summary.cold_ms = cold.execution_ms;
    summary.median_ms = percentile(timings, 0.50);
    summary.mean_ms = mean(timings);
    summary.minimum_ms = sorted.empty() ? 0.0 : sorted.front();
    summary.maximum_ms = sorted.empty() ? 0.0 : sorted.back();
    summary.standard_deviation_ms = standard_deviation(timings);
    summary.p95_ms = percentile(timings, 0.95);
    summary.p99_ms = percentile(timings, 0.99);
    summary.throughput_per_second = summary.median_ms > 0.0
        ? definition.throughput_units / (summary.median_ms / 1000.0)
        : 0.0;
    summary.mean_cpu_utilization_percent = mean(cpu);
    summary.mean_process_cpu_equivalent_cores = mean(cpu_cores);
    summary.peak_memory_bytes = peak_memory;
    summary.task_count = definition.task_count;
    summary.max_concurrency = diagnostic.max_concurrency;
    summary.scheduler_decisions = diagnostic.scheduler_decisions;
    summary.cache_hits = diagnostic.cache_hits;
    summary.stable_plan_reuse = diagnostic.stable_plan_reuse;
    summary.checksum = checksum;
    summary.expected_checksum = expected;
    summary.correct = correct && checksum == expected;
    summary.valid_for_ranking = summary.correct;
    summary.workers = options.workers;
    summary.seed = options.seed;
    summary.unit_name = definition.unit_name;
    return summary;
}

struct BenchmarkOutputs
{
    std::vector<RawSample> raw;
    std::vector<Summary> summaries;
    std::vector<TraceEnvelope> traces;
};

inline std::vector<NestedExecutionTraceRecord> bounded_trace_export(
    const std::vector<NestedExecutionTraceRecord>& records)
{
    constexpr std::size_t maximum_exported_records = 1024;
    constexpr std::size_t maximum_per_signature = 32;
    using Signature = std::tuple<std::size_t, bool, std::string, std::string, std::string>;
    std::map<Signature, std::size_t> counts;
    std::vector<NestedExecutionTraceRecord> selected;
    selected.reserve(std::min(maximum_exported_records, records.size()));
    for (const auto& record : records)
    {
        const Signature signature{
            record.depth, record.parallel, record.backend, record.decision_reason, record.phase};
        const bool important = record.exceptional || record.parallel || record.depth <= 2;
        const std::size_t per_signature_limit = important ? 128 : maximum_per_signature;
        std::size_t& count = counts[signature];
        if (count++ >= per_signature_limit)
            continue;
        selected.push_back(record);
        if (selected.size() >= maximum_exported_records)
            break;
    }
    return selected;
}

inline void append_benchmark_backend_confirmation(
    const CaseDefinition& definition,
    const ModeSpec& mode,
    const Options& options,
    std::size_t observed_concurrency,
    const std::set<std::string>& used_backends,
    std::vector<NestedExecutionTraceRecord>& records)
{
    for (const auto& backend : used_backends)
    {
        const bool already_confirmed = std::any_of(
            records.begin(), records.end(),
            [&](const NestedExecutionTraceRecord& record)
            {
                return record.parallel && record.backend_confirmed && record.backend == backend;
            });
        if (already_confirmed)
            continue;
        NestedExecutionTraceRecord record;
        record.depth = 1;
        record.iterations = definition.task_count;
        record.phase = "benchmark_backend_confirmation";
        record.requested_backend = backend_name(mode.backend);
        record.backend = backend;
        record.backend_confirmed = true;
        record.runtime_concurrency = std::max<std::size_t>(1, observed_concurrency);
        record.policy = "benchmark_controlled";
        record.mechanism = "direct_execution";
        record.decision_reason = "benchmark_wrapper_confirmed_backend";
        record.parallel = true;
        record.requested_budget = options.workers;
        record.effective_budget = std::min(
            options.workers, std::max<std::size_t>(1, definition.task_count));
        records.push_back(std::move(record));
    }
}

inline void run_case(const CaseDefinition& definition,
                     const std::vector<ModeSpec>& modes,
                     const Options& options,
                     BenchmarkOutputs& outputs)
{
    for (std::size_t mode_index = 0; mode_index < modes.size(); ++mode_index)
    {
        const ModeSpec& mode = modes[mode_index];
        std::cout << '[' << (mode_index + 1) << '/' << modes.size() << "] "
                  << definition.preset << " / " << mode.name << std::flush;

        clear_runtime_learning();
        const ExecutionEngineType configured_engine = mode.kind == ModeKind::SmartForcedBackend
            ? mode.backend
            : ExecutionEngineType::Auto;
        ScopedConfig config(options.workers, configured_engine, false);

        RawSample cold = measure_once(definition, mode, options, 0, "cold");
        if (!cold.correct)
            throw std::runtime_error("cold correctness failure in " + definition.preset + " / "
                                     + mode.name + ": " + cold.exception_status);

        for (std::size_t warmup = 0; warmup < options.warmups; ++warmup)
        {
            RawSample sample = measure_once(definition, mode, options, warmup + 1, "warmup");
            if (!sample.correct)
                throw std::runtime_error("warm-up correctness failure in " + definition.preset
                                         + " / " + mode.name);
        }

        std::vector<RawSample> warm;
        warm.reserve(options.repetitions);
        for (std::size_t repetition = 0; repetition < options.repetitions; ++repetition)
        {
            RawSample sample =
                measure_once(definition, mode, options, repetition + 1, "warm");
            if (!sample.correct)
                throw std::runtime_error("timed correctness failure in " + definition.preset
                                         + " / " + mode.name);
            warm.push_back(std::move(sample));
        }

        global_config().enable_nested_execution_trace = true;
        clear_nested_execution_trace();
        BackendUsageProbe backend_usage;
        backend_usage.reset();
        definition.reset();
        {
            ScopedBackendUsageObservation backend_observation(backend_usage);
            definition.execute(mode);
        }
        const ValidationResult diagnostic_validation = definition.validate();
        if (!diagnostic_validation.correct)
            throw std::runtime_error("diagnostic correctness failure in " + definition.preset
                                     + " / " + mode.name);
        auto trace_records = nested_execution_trace_snapshot();
        const std::size_t observed_concurrency = definition.observed_concurrency
            ? definition.observed_concurrency()
            : 1;
        append_benchmark_backend_confirmation(
            definition, mode, options, observed_concurrency, backend_usage.names(), trace_records);
        DiagnosticInfo diagnostic = diagnose(mode, trace_records, backend_usage.names());
        diagnostic.max_concurrency = std::max(
            diagnostic.max_concurrency, observed_concurrency);
        global_config().enable_nested_execution_trace = false;

        cold.actual_backend = diagnostic.actual_backend;
        cold.selected_strategy = diagnostic.selected_strategy;
        cold.selected_frontier = diagnostic.selected_frontier;
        cold.max_concurrency = diagnostic.max_concurrency;
        cold.scheduler_decisions = diagnostic.scheduler_decisions;
        cold.cache_hits = diagnostic.cache_hits;
        cold.stable_plan_reuse = diagnostic.stable_plan_reuse;
        outputs.raw.push_back(cold);
        for (auto& sample : warm)
        {
            sample.actual_backend = diagnostic.actual_backend;
            sample.selected_strategy = diagnostic.selected_strategy;
            sample.selected_frontier = diagnostic.selected_frontier;
            sample.max_concurrency = diagnostic.max_concurrency;
            sample.scheduler_decisions = diagnostic.scheduler_decisions;
            sample.cache_hits = diagnostic.cache_hits;
            sample.stable_plan_reuse = diagnostic.stable_plan_reuse;
            outputs.raw.push_back(sample);
        }
        outputs.summaries.push_back(summarize(definition, mode, options, cold, warm, diagnostic));
        if (options.trace)
        {
            for (const auto& record : bounded_trace_export(trace_records))
                outputs.traces.push_back(
                    {definition.integration, definition.workload, definition.preset, mode.name,
                     record});
        }
        std::cout << " median=" << std::fixed << std::setprecision(3)
                  << outputs.summaries.back().median_ms << " ms, backend="
                  << diagnostic.actual_backend << ", frontier=" << diagnostic.selected_frontier
                  << "\n";
    }
}

inline void attach_baselines_and_regret(std::vector<Summary>& summaries)
{
    using Key = std::tuple<std::string, std::string, std::string>;
    std::map<Key, double> sequential;
    std::map<Key, double> fastest;
    for (const auto& summary : summaries)
    {
        const Key key{summary.integration, summary.workload, summary.preset};
        if (summary.mode == "sequential" && summary.correct)
            sequential[key] = summary.median_ms;
        if (summary.valid_for_ranking && summary.median_ms > 0.0)
        {
            auto it = fastest.find(key);
            if (it == fastest.end() || summary.median_ms < it->second)
                fastest[key] = summary.median_ms;
        }
    }
    for (auto& summary : summaries)
    {
        const Key key{summary.integration, summary.workload, summary.preset};
        const auto seq = sequential.find(key);
        const auto best = fastest.find(key);
        if (seq != sequential.end() && summary.median_ms > 0.0)
            summary.speedup_over_sequential = seq->second / summary.median_ms;
        if (best != fastest.end())
        {
            summary.absolute_regret_ms = summary.median_ms - best->second;
            summary.percentage_regret = best->second > 0.0
                ? (summary.absolute_regret_ms / best->second) * 100.0
                : 0.0;
        }
    }
}

inline void write_raw_csv(const std::filesystem::path& path,
                          const std::vector<RawSample>& rows)
{
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("cannot open raw CSV: " + path.string());
    out << "integration,workload,preset,parameters,mode,requested_backend,actual_backend,"
           "selected_strategy,selected_frontier,repetition,state,execution_ms,throughput_per_second,"
           "cpu_utilization_percent,peak_memory_bytes,task_count,max_concurrency,scheduler_decisions,"
           "cache_hits,stable_plan_reuse,checksum,expected_checksum,correct,exception_status,"
           "cancellation_status,workers,seed,process_cpu_equivalent_cores\n";
    out << std::fixed << std::setprecision(6);
    for (const auto& row : rows)
    {
        out << csv_escape(row.integration) << ',' << csv_escape(row.workload) << ','
            << csv_escape(row.preset) << ',' << csv_escape(row.parameters) << ','
            << csv_escape(row.mode) << ',' << csv_escape(row.requested_backend) << ','
            << csv_escape(row.actual_backend) << ',' << csv_escape(row.selected_strategy) << ','
            << csv_escape(row.selected_frontier) << ',' << row.repetition << ','
            << csv_escape(row.state) << ',' << row.execution_ms << ','
            << row.throughput_per_second << ',' << row.cpu_utilization_percent << ','
            << row.peak_memory_bytes << ',' << row.task_count << ',' << row.max_concurrency << ','
            << row.scheduler_decisions << ',' << row.cache_hits << ',' << row.stable_plan_reuse << ','
            << row.checksum << ',' << row.expected_checksum << ',' << (row.correct ? 1 : 0) << ','
            << csv_escape(row.exception_status) << ',' << csv_escape(row.cancellation_status) << ','
            << row.workers << ',' << row.seed << ','
            << row.process_cpu_equivalent_cores << '\n';
    }
}

inline void write_summary_csv(const std::filesystem::path& path,
                              const std::vector<Summary>& rows)
{
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("cannot open summary CSV: " + path.string());
    out << "integration,workload,preset,parameters,mode,requested_backend,actual_backend,"
           "selected_strategy,selected_frontier,repetitions,warmups,cold_ms,median_ms,mean_ms,"
           "minimum_ms,maximum_ms,standard_deviation_ms,p95_ms,p99_ms,throughput_per_second,"
           "speedup_over_sequential,absolute_regret_ms,percentage_regret,mean_cpu_utilization_percent,"
           "peak_memory_bytes,task_count,max_concurrency,scheduler_decisions,cache_hits,"
           "stable_plan_reuse,checksum,expected_checksum,correct,valid_for_ranking,exception_status,"
           "cancellation_status,workers,seed,unit_name,"
           "mean_process_cpu_equivalent_cores\n";
    out << std::fixed << std::setprecision(6);
    for (const auto& row : rows)
    {
        out << csv_escape(row.integration) << ',' << csv_escape(row.workload) << ','
            << csv_escape(row.preset) << ',' << csv_escape(row.parameters) << ','
            << csv_escape(row.mode) << ',' << csv_escape(row.requested_backend) << ','
            << csv_escape(row.actual_backend) << ',' << csv_escape(row.selected_strategy) << ','
            << csv_escape(row.selected_frontier) << ',' << row.repetitions << ',' << row.warmups << ','
            << row.cold_ms << ',' << row.median_ms << ',' << row.mean_ms << ',' << row.minimum_ms << ','
            << row.maximum_ms << ',' << row.standard_deviation_ms << ',' << row.p95_ms << ','
            << row.p99_ms << ',' << row.throughput_per_second << ',' << row.speedup_over_sequential
            << ',' << row.absolute_regret_ms << ',' << row.percentage_regret << ','
            << row.mean_cpu_utilization_percent << ',' << row.peak_memory_bytes << ','
            << row.task_count << ',' << row.max_concurrency << ',' << row.scheduler_decisions << ','
            << row.cache_hits << ',' << row.stable_plan_reuse << ',' << row.checksum << ','
            << row.expected_checksum << ',' << (row.correct ? 1 : 0) << ','
            << (row.valid_for_ranking ? 1 : 0) << ',' << csv_escape(row.exception_status) << ','
            << csv_escape(row.cancellation_status) << ',' << row.workers << ',' << row.seed << ','
            << csv_escape(row.unit_name) << ','
            << row.mean_process_cpu_equivalent_cores << '\n';
    }
}

inline void write_trace_csv(const std::filesystem::path& path,
                            const std::vector<TraceEnvelope>& rows)
{
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("cannot open trace CSV: " + path.string());
    out << "integration,workload,preset,mode,root_loop_id,loop_id,parent_loop_id,callsite_hash,"
           "parent_callsite_hash,depth,iterations,phase,requested_backend,backend,backend_confirmed,"
           "runtime_concurrency,native_delegation,reused_runtime_domain,exceptional,policy,mechanism,"
           "decision_reason,cache_hit,profile_available,parallel,plan_snapshot_hit,estimated_work_ms,"
           "measured_total_ms,nested_child_ms,nested_child_calls,requested_budget,effective_budget,"
           "leased_workers,max_root_leased_workers,chunk_size,total_chunks,helpers_submitted,"
           "helpers_started,helpers_useful,helpers_cancelled,helper_retire_tail_ms,"
           "helper_completion_signal_to_wake_ms,helper_wait_count,"
           "helper_in_flight_work_drain_ms,helper_actual_blocking_wait_ms,"
           "helper_completion_epilogue_ms\n";
    out << std::fixed << std::setprecision(6);
    for (const auto& envelope : rows)
    {
        const auto& r = envelope.record;
        out << csv_escape(envelope.integration) << ',' << csv_escape(envelope.workload) << ','
            << csv_escape(envelope.preset) << ',' << csv_escape(envelope.mode) << ','
            << r.root_loop_id << ',' << r.loop_id << ',' << r.parent_loop_id << ','
            << r.callsite_hash << ',' << r.parent_callsite_hash << ',' << r.depth << ','
            << r.iterations << ',' << csv_escape(r.phase) << ',' << csv_escape(r.requested_backend)
            << ',' << csv_escape(r.backend) << ',' << (r.backend_confirmed ? 1 : 0) << ','
            << r.runtime_concurrency << ',' << (r.native_delegation ? 1 : 0) << ','
            << (r.reused_runtime_domain ? 1 : 0) << ',' << (r.exceptional ? 1 : 0) << ','
            << csv_escape(r.policy) << ',' << csv_escape(r.mechanism) << ','
            << csv_escape(r.decision_reason) << ',' << (r.cache_hit ? 1 : 0) << ','
            << (r.profile_available ? 1 : 0) << ',' << (r.parallel ? 1 : 0) << ','
            << (r.plan_snapshot_hit ? 1 : 0) << ',' << r.estimated_work_ms << ','
            << r.measured_total_ms << ',' << r.nested_child_ms << ',' << r.nested_child_calls << ','
            << r.requested_budget << ',' << r.effective_budget << ',' << r.leased_workers << ','
            << r.max_root_leased_workers << ',' << r.chunk_size << ',' << r.total_chunks << ','
            << r.helpers_submitted << ',' << r.helpers_started << ',' << r.helpers_useful << ','
            << r.helpers_cancelled << ',' << r.helper_retire_tail_ms << ','
            << r.helper_completion_signal_to_wake_ms << ',' << r.helper_wait_count << ','
            << r.helper_in_flight_work_drain_ms << ',' << r.helper_actual_blocking_wait_ms << ','
            << r.helper_completion_epilogue_ms << '\n';
    }
}

inline void write_environment_csv(
    const std::filesystem::path& path,
    const Options& options,
    const std::vector<std::pair<std::string, std::string>>& integration_metadata)
{
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("cannot open environment CSV: " + path.string());
    out << "key,value\n";
    const auto write = [&](const std::string& key, const std::string& value)
    {
        out << csv_escape(key) << ',' << csv_escape(value) << '\n';
    };
    write("smartparallel_version", SMARTPARALLEL_VERSION_STRING);
#ifdef SMARTPARALLEL_BENCHMARK_COMMIT
    write("benchmark_commit", SMARTPARALLEL_BENCHMARK_COMMIT);
#else
    write("benchmark_commit", "unknown");
#endif
    write("compiler", compiler_name());
#ifdef NDEBUG
    write("build_type", "Release");
#else
    write("build_type", "Debug");
#endif
    write("operating_system", operating_system());
    write("cpu_model", cpu_model());
    write("logical_processor_count", std::to_string(hardware_threads()));
    write("selected_worker_limit", std::to_string(options.workers));
    write("benchmark_timestamp", utc_timestamp());
    write("random_seed", std::to_string(options.seed));
    write("timed_repetitions", std::to_string(options.repetitions));
    write("warmup_repetitions", std::to_string(options.warmups));
    write("benchmark_schema_version", "2");
    write("trace_enabled", options.trace ? "1" : "0");
    write("trace_export_limit_per_case", "1024");
    write("frontier_descendant_direct_mode",
          global_config().enable_frontier_descendant_direct_mode ? "1" : "0");
    write("session_local_plan_memo",
          global_config().enable_session_local_plan_memo ? "1" : "0");
    write("root_analytical_cold_start",
          global_config().enable_root_analytical_cold_start ? "1" : "0");
    write("backend_calibration_enabled",
          global_config().enable_parallel_for_backend_calibration ? "1" : "0");
    write("trace_timing_semantics", "causal_v2");
    write("cpu_metric_semantics", "process_cpu_equivalent_cores_v2");
    write("one_tbb_available", execution_backend_available(ExecutionEngineType::OneTbb) ? "1" : "0");
    write("one_tbb_version", one_tbb_version());
    for (const auto& item : integration_metadata)
        write(item.first, item.second);
}

inline void write_outputs(const std::string& integration,
                          const Options& options,
                          BenchmarkOutputs outputs,
                          const std::vector<std::pair<std::string, std::string>>& metadata = {})
{
    attach_baselines_and_regret(outputs.summaries);
    std::filesystem::create_directories(options.output_directory);
    const std::string prefix = "v1.1.0_real_world_" + integration;
    const auto summary = options.output_directory / (prefix + "_summary.csv");
    const auto raw = options.output_directory / (prefix + "_raw.csv");
    const auto trace = options.output_directory / (prefix + "_trace.csv");
    const auto environment = options.output_directory / (prefix + "_environment.csv");
    write_summary_csv(summary, outputs.summaries);
    write_raw_csv(raw, outputs.raw);
    write_trace_csv(trace, outputs.traces);
    write_environment_csv(environment, options, metadata);

    bool correct = true;
    for (const auto& row : outputs.summaries)
        correct = correct && row.correct;
    std::cout << "Summary CSV: " << summary.string() << '\n'
              << "Raw CSV: " << raw.string() << '\n'
              << "Trace CSV: " << trace.string() << '\n'
              << "Environment CSV: " << environment.string() << '\n'
              << (correct ? "PASS" : "FAIL") << ": " << integration
              << " real-world benchmark completed.\n";
    if (!correct)
        throw std::runtime_error(integration + " correctness validation failed");
}

} // namespace smart::real_world
