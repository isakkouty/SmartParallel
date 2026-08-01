#include <smart/data/view.hpp>
#include <smart/execution/algorithms.hpp>
#include <smart/linalg/operations.hpp>
#include <smart/scientific/stencil.hpp>
#include <smart/version.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace
{
volatile double sink_value = 0.0;

std::uint64_t bits(double value)
{
    std::uint64_t output = 0;
    std::memcpy(&output, &value, sizeof(value));
    return output;
}

double elapsed_ms(const std::chrono::steady_clock::time_point& start)
{
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}


const char* operating_system() noexcept
{
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

const char* architecture() noexcept
{
#if defined(_M_X64) || defined(__x86_64__)
    return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

std::string environment_value(const char* name)
{
#if defined(_MSC_VER)
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr)
        return {};
    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

std::string cpu_description()
{
    std::string value = environment_value("SMARTPARALLEL_CPU_DESCRIPTION");
    if (value.empty()) return "unreported";
    for (char& character : value)
    {
        if (character == ',' || character == '\r' || character == '\n') character = ' ';
    }
    return value;
}

std::string digest_bits(std::uint64_t value)
{
    std::ostringstream output;
    output << std::hex << std::setw(16) << std::setfill('0') << value;
    return output.str();
}

struct ValidationRecord
{
    double result = 0.0;
    long double reference = 0.0L;
    std::string result_digest;
    std::string reference_digest;
    long double absolute_error = 0.0L;
    long double relative_error = 0.0L;
    bool execution_valid = true;
    bool reference_accuracy_pass = true;
};

ValidationRecord scalar_validation(double result, long double reference)
{
    ValidationRecord record;
    record.result = result;
    record.reference = reference;
    record.result_digest = digest_bits(bits(result));
    record.reference_digest = "scalar-reference";
    record.absolute_error = std::abs(static_cast<long double>(result) - reference);
    const long double denominator = std::abs(reference);
    record.relative_error = denominator == 0.0L
        ? record.absolute_error : record.absolute_error / denominator;
    if (std::isnan(static_cast<double>(reference)))
    {
        record.execution_valid = std::isnan(result);
        record.reference_accuracy_pass = record.execution_valid;
    }
    else if (std::isinf(static_cast<double>(reference)))
    {
        record.execution_valid = std::isinf(result)
            && std::signbit(result) == std::signbit(static_cast<double>(reference));
        record.reference_accuracy_pass = record.execution_valid;
    }
    else
    {
        record.execution_valid = std::isfinite(result);
        record.reference_accuracy_pass = record.execution_valid
            && (record.relative_error <= 1.0e-10L
                || record.absolute_error <= 1.0e-12L);
    }
    return record;
}

template <typename Actual, typename Reference>
ValidationRecord field_validation(std::size_t count,
                                  Actual actual,
                                  Reference reference,
                                  long double absolute_tolerance = 1.0e-12L,
                                  long double relative_tolerance = 1.0e-10L)
{
    constexpr std::uint64_t offset = 1469598103934665603ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t actual_hash = offset;
    std::uint64_t reference_hash = offset;
    long double actual_checksum = 0.0L;
    long double reference_checksum = 0.0L;
    long double maximum_absolute = 0.0L;
    long double maximum_relative = 0.0L;
    bool classifications_match = true;
    bool accuracy = true;
    for (std::size_t index = 0; index < count; ++index)
    {
        const double observed = actual(index);
        const double expected = reference(index);
        const std::uint64_t observed_bits = bits(observed);
        const std::uint64_t expected_bits = bits(expected);
        actual_hash = (actual_hash ^ observed_bits) * prime;
        reference_hash = (reference_hash ^ expected_bits) * prime;
        actual_checksum += static_cast<long double>(observed);
        reference_checksum += static_cast<long double>(expected);

        const bool expected_nan = std::isnan(expected);
        const bool expected_infinite = std::isinf(expected);
        bool classification_ok = true;
        if (expected_nan)
            classification_ok = std::isnan(observed);
        else if (expected_infinite)
            classification_ok = std::isinf(observed)
                && std::signbit(observed) == std::signbit(expected);
        else
            classification_ok = std::isfinite(observed);
        classifications_match = classifications_match && classification_ok;
        if (!classification_ok)
        {
            accuracy = false;
            continue;
        }
        if (expected_nan || expected_infinite)
            continue;
        const long double absolute = std::abs(
            static_cast<long double>(observed) - static_cast<long double>(expected));
        const long double denominator = std::abs(static_cast<long double>(expected));
        const long double relative = denominator == 0.0L ? absolute : absolute / denominator;
        maximum_absolute = std::max(maximum_absolute, absolute);
        maximum_relative = std::max(maximum_relative, relative);
        accuracy = accuracy
            && (absolute <= absolute_tolerance || relative <= relative_tolerance);
    }

    ValidationRecord record;
    record.result = static_cast<double>(actual_checksum);
    record.reference = reference_checksum;
    record.result_digest = digest_bits(actual_hash);
    record.reference_digest = digest_bits(reference_hash);
    record.absolute_error = maximum_absolute;
    record.relative_error = maximum_relative;
    record.execution_valid = classifications_match;
    record.reference_accuracy_pass = classifications_match && accuracy;
    return record;
}

struct Writer
{
    std::ofstream stream;
    explicit Writer(const std::string& path) : stream(path)
    {
        if (!stream) throw std::runtime_error("could not open benchmark output");
        stream << "schema_version,benchmark_version,smartparallel_version,compiler,operating_system,architecture,cpu_description,operation,data_type,workload_size,view_layout,stride,phase,repetition,duration_ms,result_bits,result_digest,reference,reference_digest,absolute_error,relative_error,execution_valid,reference_accuracy_pass,reproducibility_pass,route_authentication_pass,numerical_capability_pass,scheduler,numerical_policy,evaluation_order,accumulation_algorithm,canonical_plan,requested_scheduler,worker_budget,actual_workers\n";
    }
    void row(const std::string& operation,
             std::size_t size,
             const std::string& layout,
             std::size_t stride,
             const std::string& phase,
             std::size_t repetition,
             double duration,
             const ValidationRecord& validation,
             const smart::NumericalExecutionReport& report,
             bool reproducible = true)
    {
        stream << "2,1.6.0," << SMARTPARALLEL_VERSION_STRING << ','
#if defined(__clang__)
               << "Clang " << __clang_major__ << '.' << __clang_minor__
#elif defined(__GNUC__)
               << "GCC " << __GNUC__ << '.' << __GNUC_MINOR__
#elif defined(_MSC_VER)
               << "MSVC " << _MSC_VER
#else
               << "unknown"
#endif
               << ',' << operating_system() << ',' << architecture() << ',' << cpu_description()
               << ',' << operation << ",double," << size << ',' << layout << ',' << stride
               << ',' << phase << ',' << repetition << ',' << std::setprecision(17) << duration
               << ',' << bits(validation.result) << ',' << validation.result_digest
               << ',' << static_cast<double>(validation.reference)
               << ',' << validation.reference_digest
               << ',' << static_cast<double>(validation.absolute_error)
               << ',' << static_cast<double>(validation.relative_error)
               << ',' << (validation.execution_valid ? 1 : 0)
               << ',' << (validation.reference_accuracy_pass ? 1 : 0)
               << ',' << (reproducible ? 1 : 0)
               << ',' << (report.route_authenticated ? 1 : 0)
               << ',' << (report.capability_satisfied ? 1 : 0)
               << ',' << report.scheduler
               << ',' << smart::numerical_policy_name(report.policy)
               << ',';
        switch (report.evaluation_order)
        {
            case smart::detail::EvaluationOrder::Adaptive: stream << "adaptive"; break;
            case smart::detail::EvaluationOrder::CanonicalDeterministic:
                stream << "canonical-reduction"; break;
            case smart::detail::EvaluationOrder::CanonicalPointwise:
                stream << "canonical-pointwise"; break;
        }
        stream << ',';
        switch (report.accumulation)
        {
            case smart::detail::AccumulationMethod::Native: stream << "native"; break;
            case smart::detail::AccumulationMethod::FixedPointwiseExpression:
                stream << "fixed_pointwise_expression"; break;
            case smart::detail::AccumulationMethod::CanonicalPairwise: stream << "pairwise"; break;
            case smart::detail::AccumulationMethod::Compensated: stream << "compensated"; break;
            case smart::detail::AccumulationMethod::ScaledSumOfSquares: stream << "scaled_sumsq"; break;
        }
        stream << ',' << report.canonical_plan << ',' << smart::engine_name(report.requested_engine)
               << ',' << report.requested_worker_budget
               << ',' << report.worker_count << '\n';
    }
};

std::vector<double> make_data(std::size_t size)
{
    std::vector<double> values(size);
    for (std::size_t index = 0; index < size; ++index)
        values[index] = std::sin(static_cast<double>(index) * 0.001) * 0.5
                      + static_cast<double>(static_cast<int>(index % 17) - 8) * 0.01;
    return values;
}

template <typename Function>
void repeat_case(Writer& writer,
                 const std::string& operation,
                 std::size_t size,
                 const std::string& layout,
                 std::size_t stride,
                 std::size_t repetitions,
                 long double reference,
                 Function function)
{
    std::string stable_digest;
    for (std::size_t repetition = 0; repetition <= repetitions; ++repetition)
    {
        const auto start = std::chrono::steady_clock::now();
        const double result = function();
        const double duration = elapsed_ms(start);
        sink_value += result * 1.0e-300;
        const auto report = smart::global_last_numerical_execution_report();
        const ValidationRecord validation = scalar_validation(result, reference);
        bool reproducible = true;
        if (repetition == 0) stable_digest = validation.result_digest;
        else reproducible = report.policy == smart::NumericalPolicy::Fast
            || validation.result_digest == stable_digest;
        writer.row(operation, size, layout, stride,
                   repetition == 0 ? "first_call" : "stable",
                   repetition, duration, validation, report, reproducible);
    }
}

smart::NumericalExecutionReport direct_sequential_report(const char* operation)
{
    smart::NumericalExecutionReport report;
    report.operation = operation;
    report.policy = smart::NumericalPolicy::Fast;
    report.evaluation_order = smart::detail::EvaluationOrder::Adaptive;
    report.accumulation = smart::detail::AccumulationMethod::Native;
    report.canonical_plan = "none";
    report.scheduler = "DirectSequential";
    report.parallel = false;
    report.requested_worker_budget = 1;
    report.worker_count = 1;
    report.route_authenticated = true;
    report.capability_satisfied = true;
    return report;
}

template <typename Function>
void repeat_direct_case(Writer& writer,
                        const std::string& operation,
                        std::size_t size,
                        const std::string& layout,
                        std::size_t stride,
                        std::size_t repetitions,
                        long double reference,
                        Function function)
{
    const auto report = direct_sequential_report(operation.c_str());
    for (std::size_t repetition = 0; repetition <= repetitions; ++repetition)
    {
        const auto start = std::chrono::steady_clock::now();
        const double result = function();
        const double duration = elapsed_ms(start);
        sink_value += result * 1.0e-300;
        writer.row(operation, size, layout, stride,
                   repetition == 0 ? "first_call" : "stable",
                   repetition, duration, scalar_validation(result, reference), report, true);
    }
}

template <typename Prepare, typename Execute, typename Validate>
void repeat_field_case(Writer& writer,
                       const std::string& operation,
                       std::size_t size,
                       const std::string& layout,
                       std::size_t stride,
                       std::size_t repetitions,
                       Prepare prepare,
                       Execute execute,
                       Validate validate)
{
    std::string stable_digest;
    for (std::size_t repetition = 0; repetition <= repetitions; ++repetition)
    {
        prepare();
        const auto start = std::chrono::steady_clock::now();
        execute();
        const double duration = elapsed_ms(start);
        const auto report = smart::global_last_numerical_execution_report();
        const ValidationRecord validation = validate();
        sink_value += validation.result * 1.0e-300;
        bool reproducible = true;
        if (repetition == 0) stable_digest = validation.result_digest;
        else reproducible = report.policy == smart::NumericalPolicy::Fast
            || validation.result_digest == stable_digest;
        writer.row(operation, size, layout, stride,
                   repetition == 0 ? "first_call" : "stable",
                   repetition, duration, validation, report, reproducible);
    }
}

template <typename Prepare, typename Execute, typename Validate>
void repeat_direct_field_case(Writer& writer,
                              const std::string& operation,
                              std::size_t size,
                              const std::string& layout,
                              std::size_t stride,
                              std::size_t repetitions,
                              Prepare prepare,
                              Execute execute,
                              Validate validate)
{
    const auto report = direct_sequential_report(operation.c_str());
    for (std::size_t repetition = 0; repetition <= repetitions; ++repetition)
    {
        prepare();
        const auto start = std::chrono::steady_clock::now();
        execute();
        const double duration = elapsed_ms(start);
        const ValidationRecord validation = validate();
        sink_value += validation.result * 1.0e-300;
        writer.row(operation, size, layout, stride,
                   repetition == 0 ? "first_call" : "stable",
                   repetition, duration, validation, report, true);
    }
}

void direct_stencil(const double* input,
                    double* output,
                    std::size_t rows,
                    std::size_t columns,
                    std::size_t stride)
{
    for (std::size_t row = 0; row < rows; ++row)
    {
        for (std::size_t column = 0; column < columns; ++column)
        {
            const std::size_t index = row * stride + column;
            if (row == 0 || column == 0 || row + 1 == rows || column + 1 == columns)
            {
                output[index] = input[index];
                continue;
            }
            double value = 0.5 * input[index];
            value += 0.125 * input[(row - 1) * stride + column];
            value += 0.125 * input[(row + 1) * stride + column];
            value += 0.125 * input[index - 1];
            value += 0.125 * input[index + 1];
            output[index] = value;
        }
    }
}

void configure_benchmark(std::size_t workers,
                         smart::ExecutionEngineType engine = smart::ExecutionEngineType::ThreadPool)
{
    auto& config = smart::global_config();
    config.execution_engine = engine;
    config.enable_experience = false;
    config.enable_experience_ranking = false;
    config.enable_online_exploration = false;
    config.enable_parallel_for_backend_calibration = false;
    config.enable_parallel_for_auto_profiling = false;
    config.enable_parallel_for_profile_cache = false;
    config.enable_parallel_algorithm_hot_dispatch = false;
    config.nested_root_concurrency_budget = workers;
    config.nested_min_iterations_per_worker = 1;
    config.nested_min_parallel_work_ms = 0.0;
    config.parallel_for_estimated_overhead_ms = 0.0;
    config.parallel_for_minimum_predicted_speedup = 0.0;
    config.small_workload_iteration_threshold = 0;
    config.cheap_workload_sequential_threshold = 0;
    ++config.parallel_for_policy_generation;
}
}

int main(int argc, char** argv)
{
    try
    {
        const std::string output_path = argc > 1 ? argv[1] : "v1.6.0_scientific_raw.csv";
        const std::size_t repetitions = argc > 2 ? static_cast<std::size_t>(std::stoull(argv[2])) : 7;
        Writer writer(output_path);
        configure_benchmark(8);
        const std::vector<std::size_t> sizes{1024, 65536, 1048576};
        for (std::size_t size : sizes)
        {
            auto values = make_data(size);
            auto other = make_data(size);
            std::reverse(other.begin(), other.end());
            long double sum_reference = 0.0L, dot_reference = 0.0L, norm_reference = 0.0L;
            for (std::size_t i = 0; i < size; ++i)
            {
                sum_reference += static_cast<long double>(values[i]);
                dot_reference += static_cast<long double>(values[i]) * static_cast<long double>(other[i]);
                norm_reference += static_cast<long double>(values[i]) * static_cast<long double>(values[i]);
            }
            norm_reference = std::sqrt(norm_reference);
            auto x = smart::data::VectorView<const double>::contiguous(values.data(), {size});
            auto y = smart::data::VectorView<const double>::contiguous(other.data(), {size});
            for (auto policy : {smart::NumericalPolicy::Fast,
                                smart::NumericalPolicy::Reproducible,
                                smart::NumericalPolicy::Accurate})
            {
                repeat_case(writer, "sum", size, "contiguous", 1, repetitions, sum_reference,
                    [&] { return smart::parallel_reduce(values.begin(), values.end(), 0.0,
                                smart::NumericalOptions{policy}); });
                repeat_case(writer, "dot", size, "contiguous", 1, repetitions, dot_reference,
                    [&] { return smart::linalg::dot(x, y, smart::NumericalOptions{policy}); });
                repeat_case(writer, "norm", size, "contiguous", 1, repetitions, norm_reference,
                    [&] { return smart::linalg::norm(x, smart::NumericalOptions{policy}); });
            }
            repeat_case(writer, "sum_legacy_fast", size, "contiguous", 1, repetitions, sum_reference,
                [&]
                {
                    double result = smart::parallel_reduce(values.begin(), values.end(), 0.0);
                    smart::detail::authenticate_numerical_execution("sum_legacy_fast",
                        smart::NumericalPolicy::Fast,
                        smart::detail::AccumulationMethod::Native, "none");
                    return result;
                });

            repeat_direct_case(writer, "sum_direct_sequential", size, "contiguous", 1,
                               repetitions, sum_reference,
                [&]
                {
                    double result = 0.0;
                    for (double value : values) result += value;
                    return result;
                });
            repeat_direct_case(writer, "dot_direct_sequential", size, "contiguous", 1,
                               repetitions, dot_reference,
                [&]
                {
                    double result = 0.0;
                    for (std::size_t index = 0; index < size; ++index)
                        result += values[index] * other[index];
                    return result;
                });
            repeat_direct_case(writer, "norm_direct_sequential", size, "contiguous", 1,
                               repetitions, norm_reference,
                [&]
                {
                    double squared = 0.0;
                    for (double value : values) squared += value * value;
                    return std::sqrt(squared);
                });

            std::vector<double> axpy_reference(size);
            for (std::size_t index = 0; index < size; ++index)
                axpy_reference[index] = 0.5 * values[index] + 1.0;
            for (auto policy : {smart::NumericalPolicy::Fast,
                                smart::NumericalPolicy::Reproducible,
                                smart::NumericalPolicy::Accurate})
            {
                std::vector<double> output_values(size, 1.0);
                auto output_view = smart::data::VectorView<double>::contiguous(
                    output_values.data(), {size});
                repeat_field_case(writer, "axpy", size, "contiguous", 1, repetitions,
                    [&] { std::fill(output_values.begin(), output_values.end(), 1.0); },
                    [&]
                    {
                        smart::linalg::axpy(
                            output_view, 0.5, x, smart::NumericalOptions{policy});
                    },
                    [&]
                    {
                        return field_validation(size,
                            [&](std::size_t index) { return output_values[index]; },
                            [&](std::size_t index) { return axpy_reference[index]; });
                    });
            }

            std::vector<double> direct_output(size, 1.0);
            repeat_direct_field_case(
                writer, "axpy_direct_sequential", size, "contiguous", 1, repetitions,
                [&] { std::fill(direct_output.begin(), direct_output.end(), 1.0); },
                [&]
                {
                    for (std::size_t index = 0; index < size; ++index)
                        direct_output[index] = 0.5 * values[index] + direct_output[index];
                },
                [&]
                {
                    return field_validation(size,
                        [&](std::size_t index) { return direct_output[index]; },
                        [&](std::size_t index) { return axpy_reference[index]; });
                });
        }


        // Adjacent balanced Fast-overload regression evidence. The two wrappers
        // execute the same retained Fast implementation; alternating order avoids
        // treating cache/runtime position as wrapper overhead.
        {
            const std::size_t regression_size = 1048576;
            auto regression_values = make_data(regression_size);
            long double regression_reference = 0.0L;
            for (double value : regression_values)
                regression_reference += static_cast<long double>(value);
            auto record_policy = [&](std::size_t repetition)
            {
                const auto start = std::chrono::steady_clock::now();
                const double result = smart::parallel_reduce(
                    regression_values.begin(), regression_values.end(), 0.0,
                    smart::NumericalOptions{smart::NumericalPolicy::Fast});
                const double duration = elapsed_ms(start);
                const auto report = smart::global_last_numerical_execution_report();
                writer.row("sum_fast_regression_policy", regression_size,
                           "contiguous", 1,
                           repetition == 0 ? "first_call" : "stable",
                           repetition, duration,
                           scalar_validation(result, regression_reference), report, true);
            };
            auto record_legacy = [&](std::size_t repetition)
            {
                const auto start = std::chrono::steady_clock::now();
                const double result = smart::parallel_reduce(
                    regression_values.begin(), regression_values.end(), 0.0);
                smart::detail::authenticate_numerical_execution(
                    "sum_fast_regression_legacy", smart::NumericalPolicy::Fast,
                    smart::detail::AccumulationMethod::Native, "none");
                const double duration = elapsed_ms(start);
                const auto report = smart::global_last_numerical_execution_report();
                writer.row("sum_fast_regression_legacy", regression_size,
                           "contiguous", 1,
                           repetition == 0 ? "first_call" : "stable",
                           repetition, duration,
                           scalar_validation(result, regression_reference), report, true);
            };
            for (std::size_t repetition = 0; repetition <= repetitions; ++repetition)
            {
                if ((repetition & 1u) == 0)
                {
                    record_policy(repetition);
                    record_legacy(repetition);
                }
                else
                {
                    record_legacy(repetition);
                    record_policy(repetition);
                }
            }
        }


        // Explicit strided-view evidence at a representative memory-scale size.
        {
            const std::size_t size = 65536;
            const std::size_t x_stride = 2;
            const std::size_t y_stride = 3;
            std::vector<double> x_storage(size * x_stride, -11.0);
            std::vector<double> y_storage(size * y_stride, -13.0);
            for (std::size_t index = 0; index < size; ++index)
            {
                x_storage[index * x_stride] = std::sin(static_cast<double>(index) * 0.001);
                y_storage[index * y_stride] = std::cos(static_cast<double>(index) * 0.001);
            }
            const auto x = smart::data::VectorView<const double>(
                x_storage.data(), {size}, {x_stride});
            const auto y = smart::data::VectorView<const double>(
                y_storage.data(), {size}, {y_stride});
            long double dot_reference = 0.0L;
            long double norm_reference = 0.0L;
            for (std::size_t index = 0; index < size; ++index)
            {
                const long double value = x_storage[index * x_stride];
                dot_reference += value * y_storage[index * y_stride];
                norm_reference += value * value;
            }
            norm_reference = std::sqrt(norm_reference);
            for (auto policy : {smart::NumericalPolicy::Fast,
                                smart::NumericalPolicy::Reproducible,
                                smart::NumericalPolicy::Accurate})
            {
                repeat_case(writer, "dot_strided", size, "strided", x_stride,
                            repetitions, dot_reference,
                    [&] { return smart::linalg::dot(x, y, smart::NumericalOptions{policy}); });
                repeat_case(writer, "norm_strided", size, "strided", x_stride,
                            repetitions, norm_reference,
                    [&] { return smart::linalg::norm(x, smart::NumericalOptions{policy}); });

                std::vector<double> output_storage(size * y_stride, -17.0);
                std::vector<double> axpy_reference(size);
                for (std::size_t index = 0; index < size; ++index)
                    axpy_reference[index] = 1.0 + 0.5 * x_storage[index * x_stride];
                auto output = smart::data::VectorView<double>(
                    output_storage.data(), {size}, {y_stride});
                repeat_field_case(writer, "axpy_strided", size, "strided", y_stride,
                    repetitions,
                    [&]
                    {
                        for (std::size_t index = 0; index < size; ++index)
                            output_storage[index * y_stride] = 1.0;
                    },
                    [&]
                    {
                        smart::linalg::axpy(
                            output, 0.5, x, smart::NumericalOptions{policy});
                    },
                    [&]
                    {
                        return field_validation(size,
                            [&](std::size_t index)
                            { return output_storage[index * y_stride]; },
                            [&](std::size_t index) { return axpy_reference[index]; });
                    });
            }
        }

        // Adversarial cancellation evidence for policy error comparisons.
        std::vector<double> adversarial;
        adversarial.reserve(9000);
        for (int i = 0; i < 3000; ++i)
        {
            adversarial.push_back(1.0e16);
            adversarial.push_back(1.0);
            adversarial.push_back(-1.0e16);
        }
        std::vector<double> adversarial_ones(adversarial.size(), 1.0);
        auto adversarial_view = smart::data::VectorView<const double>::contiguous(
            adversarial.data(), {adversarial.size()});
        auto adversarial_ones_view = smart::data::VectorView<const double>::contiguous(
            adversarial_ones.data(), {adversarial_ones.size()});
        for (auto policy : {smart::NumericalPolicy::Fast,
                            smart::NumericalPolicy::Reproducible,
                            smart::NumericalPolicy::Accurate})
        {
            repeat_case(writer, "sum_adversarial", adversarial.size(), "contiguous", 1,
                        repetitions, 3000.0L,
                [&] { return smart::parallel_reduce(adversarial.begin(), adversarial.end(), 0.0,
                                                    smart::NumericalOptions{policy}); });
            repeat_case(writer, "dot_adversarial", adversarial.size(), "contiguous", 1,
                        repetitions, 3000.0L,
                [&] { return smart::linalg::dot(adversarial_view, adversarial_ones_view,
                                               smart::NumericalOptions{policy}); });
        }

        // Canonical reduction scaling and worker-count reproducibility matrix.
        auto scaling_values = make_data(1048576);
        long double scaling_reference = 0.0L;
        for (double value : scaling_values) scaling_reference += static_cast<long double>(value);
        std::vector<smart::ExecutionEngineType> scaling_engines{
            smart::ExecutionEngineType::ThreadPool,
            smart::ExecutionEngineType::StaticThread};
#if SMARTPARALLEL_HAS_TBB
        scaling_engines.push_back(smart::ExecutionEngineType::OneTbb);
#endif
        for (auto engine : scaling_engines)
        {
            for (std::size_t budget : {std::size_t{1}, std::size_t{2}, std::size_t{4}, std::size_t{8}})
            {
                configure_benchmark(budget, engine);
                repeat_case(writer, "sum_scaling", scaling_values.size(), "contiguous", 1,
                            repetitions, scaling_reference,
                    [&] { return smart::parallel_reduce(
                        scaling_values.begin(), scaling_values.end(), 0.0,
                        smart::NumericalOptions{smart::NumericalPolicy::Reproducible}); });
            }
        }
        // Canonical pointwise reproducibility matrix across engines and worker budgets.
        {
            const std::size_t pointwise_size = 262144;
            auto pointwise_x_storage = make_data(pointwise_size);
            std::vector<double> pointwise_y_storage(pointwise_size, 1.0);
            std::vector<double> pointwise_reference(pointwise_size);
            for (std::size_t index = 0; index < pointwise_size; ++index)
                pointwise_reference[index] = 0.375 * pointwise_x_storage[index] + 1.0;
            const auto pointwise_x = smart::data::VectorView<const double>::contiguous(
                pointwise_x_storage.data(), {pointwise_size});
            auto pointwise_y = smart::data::VectorView<double>::contiguous(
                pointwise_y_storage.data(), {pointwise_size});

            constexpr std::size_t matrix_rows = 512;
            constexpr std::size_t matrix_columns = 513;
            std::vector<double> matrix_input(matrix_rows * matrix_columns);
            std::vector<double> matrix_output(matrix_rows * matrix_columns, -1.0);
            std::vector<double> matrix_reference(matrix_rows * matrix_columns, -1.0);
            for (std::size_t row = 0; row < matrix_rows; ++row)
                for (std::size_t column = 0; column < matrix_columns; ++column)
                    matrix_input[row * matrix_columns + column] =
                        2.0 + std::sin(static_cast<double>(row) * 0.01)
                            + std::cos(static_cast<double>(column) * 0.02);
            direct_stencil(matrix_input.data(), matrix_reference.data(),
                           matrix_rows, matrix_columns, matrix_columns);
            const auto matrix_in = smart::data::MatrixView<const double>::contiguous(
                matrix_input.data(), {matrix_rows, matrix_columns});
            auto matrix_out = smart::data::MatrixView<double>::contiguous(
                matrix_output.data(), {matrix_rows, matrix_columns});
            const smart::scientific::Stencil2DCoefficients<double> matrix_coefficients{
                0.5, 0.125, 0.125, 0.125, 0.125};

            for (auto engine : scaling_engines)
            {
                for (std::size_t budget : {std::size_t{1}, std::size_t{2},
                                           std::size_t{4}, std::size_t{8}})
                {
                    configure_benchmark(budget, engine);
                    repeat_field_case(
                        writer, "axpy_pointwise_matrix", pointwise_size,
                        "contiguous", 1, repetitions,
                        [&]
                        {
                            std::fill(pointwise_y_storage.begin(),
                                      pointwise_y_storage.end(), 1.0);
                        },
                        [&]
                        {
                            smart::linalg::axpy(
                                pointwise_y, 0.375, pointwise_x,
                                smart::NumericalOptions{
                                    smart::NumericalPolicy::Reproducible});
                        },
                        [&]
                        {
                            return field_validation(pointwise_size,
                                [&](std::size_t index)
                                { return pointwise_y_storage[index]; },
                                [&](std::size_t index)
                                { return pointwise_reference[index]; });
                        });
                    repeat_field_case(
                        writer, "stencil_pointwise_matrix",
                        matrix_rows * matrix_columns, "contiguous", matrix_columns,
                        repetitions,
                        [&]
                        {
                            std::fill(matrix_output.begin(), matrix_output.end(), -777.0);
                        },
                        [&]
                        {
                            smart::scientific::stencil_2d(
                                matrix_in, matrix_out, matrix_coefficients,
                                smart::NumericalOptions{
                                    smart::NumericalPolicy::Reproducible});
                        },
                        [&]
                        {
                            return field_validation(matrix_output.size(),
                                [&](std::size_t index) { return matrix_output[index]; },
                                [&](std::size_t index) { return matrix_reference[index]; });
                        });
                }
            }
        }
        configure_benchmark(8);

        for (std::size_t side : {64u, 256u, 1024u})
        {
            std::vector<double> input(side * side), output(side * side);
            std::vector<double> reference_output(side * side);
            for (std::size_t row = 0; row < side; ++row)
                for (std::size_t column = 0; column < side; ++column)
                    input[row * side + column] = 10.0
                        + std::sin(static_cast<double>(row) * 0.03)
                        + std::cos(static_cast<double>(column) * 0.02)
                        + static_cast<double>((row * 17 + column * 13) % 29) * 0.01;
            direct_stencil(
                input.data(), reference_output.data(), side, side, side);
            const auto in = smart::data::MatrixView<const double>::contiguous(
                input.data(), {side, side});
            auto out = smart::data::MatrixView<double>::contiguous(
                output.data(), {side, side});
            const smart::scientific::Stencil2DCoefficients<double> coefficients{
                0.5, 0.125, 0.125, 0.125, 0.125};
            for (auto policy : {smart::NumericalPolicy::Fast,
                                smart::NumericalPolicy::Reproducible,
                                smart::NumericalPolicy::Accurate})
            {
                repeat_field_case(
                    writer, "stencil_2d", side * side, "contiguous", side,
                    repetitions,
                    [&] { std::fill(output.begin(), output.end(), -777.0); },
                    [&]
                    {
                        smart::scientific::stencil_2d(
                            in, out, coefficients, smart::NumericalOptions{policy});
                    },
                    [&]
                    {
                        return field_validation(side * side,
                            [&](std::size_t index) { return output[index]; },
                            [&](std::size_t index) { return reference_output[index]; });
                    });
            }
            repeat_direct_field_case(
                writer, "stencil_2d_direct_sequential", side * side,
                "contiguous", side, repetitions,
                [&] { std::fill(output.begin(), output.end(), -777.0); },
                [&]
                {
                    direct_stencil(input.data(), output.data(), side, side, side);
                },
                [&]
                {
                    return field_validation(side * side,
                        [&](std::size_t index) { return output[index]; },
                        [&](std::size_t index) { return reference_output[index]; });
                });
        }

        // Padded-row stencil evidence exercises non-contiguous MatrixView execution.
        {
            const std::size_t side = 256;
            const std::size_t stride = side + 7;
            std::vector<double> input(side * stride, -1.0), output(side * stride, -2.0);
            std::vector<double> reference_output(side * stride, -3.0);
            for (std::size_t row = 0; row < side; ++row)
                for (std::size_t column = 0; column < side; ++column)
                    input[row * stride + column] = 10.0
                        + std::sin(static_cast<double>(row) * 0.03)
                        + std::cos(static_cast<double>(column) * 0.02)
                        + static_cast<double>((row + column * 7) % 23) * 0.01;
            direct_stencil(
                input.data(), reference_output.data(), side, side, stride);
            const auto in = smart::data::MatrixView<const double>(
                input.data(), {side, side}, {stride, 1});
            auto out = smart::data::MatrixView<double>(
                output.data(), {side, side}, {stride, 1});
            const smart::scientific::Stencil2DCoefficients<double> coefficients{
                0.5, 0.125, 0.125, 0.125, 0.125};
            for (auto policy : {smart::NumericalPolicy::Fast,
                                smart::NumericalPolicy::Reproducible,
                                smart::NumericalPolicy::Accurate})
            {
                repeat_field_case(
                    writer, "stencil_2d_padded", side * side, "padded", stride,
                    repetitions,
                    [&]
                    {
                        for (std::size_t row = 0; row < side; ++row)
                            std::fill_n(output.data() + row * stride, side, -777.0);
                    },
                    [&]
                    {
                        smart::scientific::stencil_2d(
                            in, out, coefficients, smart::NumericalOptions{policy});
                    },
                    [&]
                    {
                        return field_validation(side * side,
                            [&](std::size_t index)
                            {
                                return output[(index / side) * stride + index % side];
                            },
                            [&](std::size_t index)
                            {
                                return reference_output[(index / side) * stride + index % side];
                            });
                    });
            }
        }

        // Application-level heat diffusion benchmark (20 iterations per sample).
        const std::size_t side = 512;
        std::vector<double> initial_heat(side * side);
        for (std::size_t row = 0; row < side; ++row)
        {
            for (std::size_t column = 0; column < side; ++column)
            {
                const double row_wave = std::sin(static_cast<double>(row) * 0.021);
                const double column_wave = std::cos(static_cast<double>(column) * 0.017);
                const double hotspot = (row > side / 3 && row < side / 3 + 24
                                     && column > side / 2 && column < side / 2 + 24)
                    ? 65.0 : 0.0;
                initial_heat[row * side + column] = 10.0 + row_wave + column_wave + hotspot;
            }
        }
        for (std::size_t i = 0; i < side; ++i)
        {
            initial_heat[i] = 90.0 + static_cast<double>(i % 11);
            initial_heat[(side - 1) * side + i] = 70.0 + static_cast<double>(i % 13);
            initial_heat[i * side] = 80.0 + static_cast<double>(i % 7);
            initial_heat[i * side + side - 1] = 60.0 + static_cast<double>(i % 5);
        }
        std::vector<double> heat_reference_a = initial_heat;
        std::vector<double> heat_reference_b(side * side, 0.0);
        for (int iteration = 0; iteration < 20; ++iteration)
        {
            direct_stencil(heat_reference_a.data(), heat_reference_b.data(), side, side, side);
            heat_reference_a.swap(heat_reference_b);
        }
        std::vector<double> heat_a(side * side), heat_b(side * side);
        const smart::scientific::Stencil2DCoefficients<double> heat_coefficients{
            0.5, 0.125, 0.125, 0.125, 0.125};
        for (auto policy : {smart::NumericalPolicy::Fast,
                            smart::NumericalPolicy::Reproducible,
                            smart::NumericalPolicy::Accurate})
        {
            repeat_field_case(
                writer, "heat_diffusion_20", side * side, "contiguous", side,
                repetitions,
                [&]
                {
                    heat_a = initial_heat;
                    std::fill(heat_b.begin(), heat_b.end(), -777.0);
                },
                [&]
                {
                    for (int iteration = 0; iteration < 20; ++iteration)
                    {
                        smart::scientific::stencil_2d(
                            smart::data::MatrixView<const double>::contiguous(
                                heat_a.data(), {side, side}),
                            smart::data::MatrixView<double>::contiguous(
                                heat_b.data(), {side, side}),
                            heat_coefficients, smart::NumericalOptions{policy});
                        heat_a.swap(heat_b);
                    }
                },
                [&]
                {
                    return field_validation(side * side,
                        [&](std::size_t index) { return heat_a[index]; },
                        [&](std::size_t index) { return heat_reference_a[index]; });
                });
        }

        repeat_direct_field_case(
            writer, "heat_diffusion_20_direct_sequential", side * side,
            "contiguous", side, repetitions,
            [&]
            {
                heat_a = initial_heat;
                std::fill(heat_b.begin(), heat_b.end(), -777.0);
            },
            [&]
            {
                for (int iteration = 0; iteration < 20; ++iteration)
                {
                    direct_stencil(heat_a.data(), heat_b.data(), side, side, side);
                    heat_a.swap(heat_b);
                }
            },
            [&]
            {
                return field_validation(side * side,
                    [&](std::size_t index) { return heat_a[index]; },
                    [&](std::size_t index) { return heat_reference_a[index]; });
            });

        std::cout << "SmartParallel v1.6 benchmark raw data: " << output_path << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "v1.6 benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
