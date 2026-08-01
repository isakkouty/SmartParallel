#include <smart/data/view.hpp>
#include <smart/execution/algorithms.hpp>
#include <smart/linalg/operations.hpp>
#include <smart/scientific/stencil.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename T>
std::uint64_t bits(T value)
{
    std::uint64_t output = 0;
    static_assert(sizeof(T) <= sizeof(output));
    std::memcpy(&output, &value, sizeof(T));
    return output;
}

struct ConfigGuard
{
    smart::Config saved = smart::global_config();
    ~ConfigGuard() { smart::global_config() = saved; }
};

void configure(smart::ExecutionEngineType engine, std::size_t workers)
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

void test_views()
{
    std::vector<double> storage(40, -1.0);
    auto matrix = smart::data::MatrixView<double>(storage.data(), {3, 4}, {7, 1});
    require(!matrix.is_contiguous(), "padded matrix reported contiguous");
    matrix(2, 3) = 9.0;
    require(storage[17] == 9.0, "matrix stride indexing failed");
    smart::data::MatrixView<const double> readonly(matrix);
    require(readonly(2, 3) == 9.0, "const view conversion failed");
    require(readonly.size() == matrix.size() && !readonly.empty(),
            "const view conversion lost logical shape state");

    auto contiguous = smart::data::MatrixView<double>::contiguous(storage.data(), {4, 5});
    require(contiguous.is_contiguous(), "contiguous matrix not detected");
    require(contiguous.size() == 20, "view logical size wrong");
    auto empty = smart::data::VectorView<double>::contiguous(nullptr, {0});
    require(empty.empty(), "zero view not empty");
    auto empty_matrix = smart::data::MatrixView<double>::contiguous(
        nullptr, {0, std::numeric_limits<std::size_t>::max()});
    require(empty_matrix.empty() && empty_matrix.is_contiguous()
                && empty_matrix.stride(1) == 1,
            "zero matrix with large unused extent was not supported");

    bool null_threw = false;
    try { (void)smart::data::VectorView<double>::contiguous(nullptr, {1}); }
    catch (const std::invalid_argument&) { null_threw = true; }
    require(null_threw, "non-empty null view was accepted");

    auto left = smart::data::VectorView<double>::contiguous(storage.data(), {10});
    auto same = smart::data::VectorView<double>::contiguous(storage.data(), {10});
    auto overlap = smart::data::VectorView<double>::contiguous(storage.data() + 5, {10});
    auto disjoint = smart::data::VectorView<double>::contiguous(storage.data() + 20, {10});
    require(left.overlap(same) == smart::data::OverlapKind::ExactMapping,
            "exact mapping not detected");
    require(left.overlap(overlap) == smart::data::OverlapKind::Overlap,
            "contiguous overlap not detected");
    require(left.overlap(disjoint) == smart::data::OverlapKind::Disjoint,
            "disjoint mapping not detected");

    auto unique_padded = smart::data::MatrixView<double>(storage.data(), {3, 4}, {7, 1});
    auto aliased_rows = smart::data::MatrixView<double>(storage.data(), {3, 4}, {0, 1});
    auto aliased_lattice = smart::data::MatrixView<double>(storage.data(), {3, 4}, {2, 1});
    require(unique_padded.has_unique_mapping(), "unique padded matrix was rejected");
    require(!aliased_rows.has_unique_mapping(), "zero-stride row alias was not detected");
    require(!aliased_lattice.has_unique_mapping(), "lattice alias was not detected");

    bool overflow_threw = false;
    try
    {
        (void)smart::data::MatrixView<double>(storage.data(),
            {std::numeric_limits<std::size_t>::max(), 2}, {2, 1});
    }
    catch (const std::overflow_error&) { overflow_threw = true; }
    require(overflow_threw, "view overflow was not rejected");

    alignas(64) double aligned_storage[8]{};
    auto aligned = smart::data::VectorView<double>::contiguous(
        aligned_storage, {8}, 64);
    require(aligned.declared_alignment() == 64,
            "valid declared alignment was not preserved");
    bool invalid_alignment_threw = false;
    try
    {
        (void)smart::data::VectorView<double>::contiguous(aligned_storage, {8}, 3);
    }
    catch (const std::invalid_argument&) { invalid_alignment_threw = true; }
    require(invalid_alignment_threw, "non-power-of-two alignment was accepted");
    bool false_alignment_threw = false;
    try
    {
        (void)smart::data::VectorView<double>::contiguous(aligned_storage + 1, {7}, 64);
    }
    catch (const std::invalid_argument&) { false_alignment_threw = true; }
    require(false_alignment_threw, "false pointer alignment declaration was accepted");
}

std::vector<double> adversarial_sum_data()
{
    std::vector<double> values;
    values.reserve(9000);
    for (int i = 0; i < 3000; ++i)
    {
        values.push_back(1.0e16);
        values.push_back(1.0);
        values.push_back(-1.0e16);
    }
    return values;
}

void test_reproducible_reductions()
{
    ConfigGuard guard;
    const auto values = adversarial_sum_data();
    std::vector<smart::ExecutionEngineType> engines{
        smart::ExecutionEngineType::ThreadPool,
        smart::ExecutionEngineType::StaticThread};
#if SMARTPARALLEL_HAS_TBB
    engines.push_back(smart::ExecutionEngineType::OneTbb);
#endif
    std::uint64_t expected_bits = 0;
    bool have_expected = false;
    for (auto engine : engines)
    {
        for (std::size_t workers : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{8}})
        {
            configure(engine, workers);
            for (int repeat = 0; repeat < 3; ++repeat)
            {
                const double result = smart::parallel_reduce(
                    values.begin(), values.end(), 0.0,
                    smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
                if (!have_expected) { expected_bits = bits(result); have_expected = true; }
                require(bits(result) == expected_bits,
                        "reproducible sum changed across engine/worker count");
                require(std::string(smart::global_last_numerical_execution_report().canonical_plan)
                            == smart::detail::canonical_pairwise_plan_v1,
                        "canonical plan was not authenticated");
            }
        }
    }

    const double fast = smart::parallel_reduce(values.begin(), values.end(), 0.0);
    const double accurate = smart::parallel_reduce(
        values.begin(), values.end(), 0.0,
        smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    const long double reference = 3000.0L;
    require(std::abs(static_cast<long double>(accurate) - reference)
                < std::abs(static_cast<long double>(fast) - reference),
            "accurate sum did not improve adversarial error");

    bool unsupported_threw = false;
    try
    {
        (void)smart::parallel_reduce(values.begin(), values.end(), 1.0,
            std::multiplies<>{}, smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    }
    catch (const std::invalid_argument&) { unsupported_threw = true; }
    require(unsupported_threw, "unsupported Accurate reduction did not fail clearly");

    bool capability_threw = false;
    try
    {
        smart::detail::require_numerical_capability(
            smart::detail::NumericalCapabilities{true, true, false},
            smart::NumericalPolicy::Accurate);
    }
    catch (const std::invalid_argument&) { capability_threw = true; }
    require(capability_threw, "unsupported numerical candidate was not excluded");

    // The canonical plan must not change at a leaf boundary.
    std::vector<double> boundary_values(1025);
    for (std::size_t index = 0; index < boundary_values.size(); ++index)
        boundary_values[index] = index % 3 == 0 ? 1.0e8 : static_cast<double>(index % 11) / 7.0;
    for (const std::size_t length : {std::size_t{1023}, std::size_t{1024}, std::size_t{1025}})
    {
        std::uint64_t boundary_bits = 0;
        bool boundary_initialized = false;
        for (auto engine : engines)
        {
            for (std::size_t workers : {std::size_t{1}, std::size_t{3}, std::size_t{8}})
            {
                configure(engine, workers);
                const double result = smart::parallel_reduce(
                    boundary_values.begin(), boundary_values.begin() + static_cast<std::ptrdiff_t>(length),
                    0.0, smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
                if (!boundary_initialized)
                {
                    boundary_bits = bits(result);
                    boundary_initialized = true;
                }
                require(bits(result) == boundary_bits,
                        "canonical leaf-boundary result changed across routes");
            }
        }
    }

    // Nested execution may change scheduling, but not the canonical numerical plan.
    configure(smart::ExecutionEngineType::ThreadPool, 4);
    std::vector<double> nested_results(4, 0.0);
    smart::parallel_for(std::size_t{0}, nested_results.size(), [&](std::size_t outer)
    {
        nested_results[outer] = smart::parallel_reduce(
            boundary_values.begin(), boundary_values.end(), 0.0,
            smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
    });
    for (std::size_t index = 1; index < nested_results.size(); ++index)
        require(bits(nested_results[index]) == bits(nested_results[0]),
                "nested canonical reduction changed its result");

    std::vector<double> empty;
    require(smart::parallel_reduce(empty.begin(), empty.end(), 7.0,
                smart::NumericalOptions{smart::NumericalPolicy::Reproducible}) == 7.0,
            "empty reproducible reduction did not return init");
    require(std::string(smart::global_last_numerical_execution_report().scheduler)
                == "Sequential(no-work)",
            "empty numerical execution was not authenticated as no-work");

    const double unary = smart::parallel_transform_reduce(
        boundary_values.begin(), boundary_values.end(), 5.0, std::plus<>{},
        [](double value) { return value * 0.5; },
        smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
    double unary_reference = 5.0;
    for (double value : boundary_values) unary_reference += value * 0.5;
    require(bits(unary) == bits(unary_reference),
            "canonical unary transform-reduce order differed from reference");

    std::vector<double> transform_ones(boundary_values.size(), 1.0);
    const double accurate_binary = smart::parallel_transform_reduce(
        boundary_values.begin(), boundary_values.end(), transform_ones.begin(), 0.0,
        std::plus<>{}, std::multiplies<>{},
        smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    const double accurate_reduce = smart::parallel_reduce(
        boundary_values.begin(), boundary_values.end(), 0.0,
        smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    require(bits(accurate_binary) == bits(accurate_reduce),
            "accurate binary transform-reduce did not use the compensated product plan");

    bool transform_exception = false;
    try
    {
        (void)smart::parallel_transform_reduce(
            boundary_values.begin(), boundary_values.end(), 0.0, std::plus<>{},
            [](double value)
            {
                if (value > 1.0e7) throw std::runtime_error("expected transform failure");
                return value;
            }, smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
    }
    catch (const std::runtime_error&) { transform_exception = true; }
    require(transform_exception, "canonical transform-reduce did not propagate exceptions");
}

void test_special_values()
{
    std::vector<double> nan_values{1.0, std::numeric_limits<double>::quiet_NaN(), 2.0};
    require(std::isnan(smart::parallel_reduce(
        nan_values.begin(), nan_values.end(), 0.0,
        smart::NumericalOptions{smart::NumericalPolicy::Accurate})),
        "Accurate sum did not propagate NaN classification");
    std::vector<double> infinities{std::numeric_limits<double>::infinity(),
                                  -std::numeric_limits<double>::infinity()};
    require(std::isnan(smart::parallel_reduce(
        infinities.begin(), infinities.end(), 0.0,
        smart::NumericalOptions{smart::NumericalPolicy::Accurate})),
        "opposing infinities did not produce NaN");
    std::vector<double> positive{1.0, std::numeric_limits<double>::infinity()};
    require(std::isinf(smart::parallel_reduce(
        positive.begin(), positive.end(), 0.0,
        smart::NumericalOptions{smart::NumericalPolicy::Accurate})),
        "positive infinity was not preserved");

    std::vector<double> nan_norm(1025, 0.0);
    nan_norm.front() = std::numeric_limits<double>::quiet_NaN();
    nan_norm.back() = 1.0;
    auto nan_norm_view = smart::data::VectorView<const double>::contiguous(
        nan_norm.data(), {nan_norm.size()});
    require(std::isnan(smart::linalg::norm(
        nan_norm_view, smart::NumericalOptions{smart::NumericalPolicy::Accurate})),
        "scaled norm merge lost NaN classification");

    std::vector<double> negative_zero{-0.0};
    const double zero_result = smart::parallel_reduce(
        negative_zero.begin(), negative_zero.end(), 0.0,
        smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    require(zero_result == 0.0 && !std::signbit(zero_result),
            "Accurate sum did not produce its documented canonical positive zero");

    std::vector<double> subnormal{std::numeric_limits<double>::denorm_min(),
                                  std::numeric_limits<double>::denorm_min()};
    const double subnormal_result = smart::parallel_reduce(
        subnormal.begin(), subnormal.end(), 0.0,
        smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    require(subnormal_result >= 0.0,
            "subnormal Accurate sum produced an invalid classification");
}

void test_linalg()
{
    ConfigGuard guard;
    configure(smart::ExecutionEngineType::ThreadPool, 4);
    std::vector<double> x_storage(40, 0.0), y_storage(40, 0.0);
    for (std::size_t i = 0; i < 12; ++i)
    {
        x_storage[i * 3] = static_cast<double>(i + 1);
        y_storage[i * 2] = static_cast<double>(2 * i - 3);
    }
    auto x = smart::data::VectorView<const double>(x_storage.data(), {12}, {3});
    auto y = smart::data::VectorView<double>(y_storage.data(), {12}, {2});
    smart::linalg::axpy(y, 0.5, x,
        smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
    for (std::size_t i = 0; i < 12; ++i)
        require(y(i) == static_cast<double>(2 * i - 3) + 0.5 * static_cast<double>(i + 1),
                "strided axpy produced wrong result");

    std::vector<double> a = adversarial_sum_data();
    std::vector<double> ones(a.size(), 1.0);
    auto av = smart::data::VectorView<const double>::contiguous(a.data(), {a.size()});
    auto ov = smart::data::VectorView<const double>::contiguous(ones.data(), {ones.size()});
    const double fast_dot = smart::linalg::dot(av, ov,
        smart::NumericalOptions{smart::NumericalPolicy::Fast});
    const double accurate_dot = smart::linalg::dot(av, ov,
        smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    require(std::abs(accurate_dot - 3000.0) < std::abs(fast_dot - 3000.0),
            "accurate dot did not improve cancellation error");

    std::vector<double> huge{1.0e308, 1.0e308};
    auto huge_view = smart::data::VectorView<const double>::contiguous(huge.data(), {huge.size()});
    require(std::isinf(smart::linalg::norm(huge_view,
        smart::NumericalOptions{smart::NumericalPolicy::Fast})),
        "naive norm test did not overflow as expected");
    const double robust_huge = smart::linalg::norm(huge_view,
        smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    require(std::isfinite(robust_huge) && robust_huge > 1.0e308,
            "accurate norm did not avoid overflow");

    std::vector<double> tiny{1.0e-308, 1.0e-308};
    auto tiny_view = smart::data::VectorView<const double>::contiguous(tiny.data(), {tiny.size()});
    require(smart::linalg::norm(tiny_view,
        smart::NumericalOptions{smart::NumericalPolicy::Fast}) == 0.0,
        "naive norm test did not underflow as expected");
    require(smart::linalg::norm(tiny_view,
        smart::NumericalOptions{smart::NumericalPolicy::Accurate}) > 0.0,
        "accurate norm did not avoid underflow");

    bool overlap_threw = false;
    try
    {
        auto overlapping_x = smart::data::VectorView<const double>::contiguous(y_storage.data() + 1, {5});
        auto overlapping_y = smart::data::VectorView<double>::contiguous(y_storage.data(), {5});
        smart::linalg::axpy(overlapping_y, 1.0, overlapping_x);
    }
    catch (const std::invalid_argument&) { overlap_threw = true; }
    require(overlap_threw, "AXPY partial overlap was not rejected");

    bool self_alias_threw = false;
    try
    {
        auto repeated_y = smart::data::VectorView<double>(y_storage.data(), {5}, {0});
        auto disjoint_x = smart::data::VectorView<const double>::contiguous(x_storage.data(), {5});
        smart::linalg::axpy(repeated_y, 1.0, disjoint_x);
    }
    catch (const std::invalid_argument&) { self_alias_threw = true; }
    require(self_alias_threw, "AXPY non-unique output mapping was not rejected");

    for (double alpha : {0.0, 1.0, -2.0, 1.0e100, 1.0e-100})
    {
        std::vector<double> same_storage{1.0, -2.0, 3.0, -4.0};
        const auto original = same_storage;
        auto same_y = smart::data::VectorView<double>::contiguous(
            same_storage.data(), {same_storage.size()});
        smart::data::VectorView<const double> same_x(same_y);
        smart::linalg::axpy(
            same_y, alpha, same_x,
            smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
        for (std::size_t index = 0; index < same_storage.size(); ++index)
            require(same_storage[index] == alpha * original[index] + original[index],
                    "AXPY exact same-view contract failed");
    }

    bool mismatch_threw = false;
    try
    {
        auto short_view = smart::data::VectorView<const double>::contiguous(a.data(), {3});
        auto long_view = smart::data::VectorView<const double>::contiguous(ones.data(), {4});
        (void)smart::linalg::dot(short_view, long_view);
    }
    catch (const std::invalid_argument&) { mismatch_threw = true; }
    require(mismatch_threw, "dot mismatched extents were accepted");

    auto empty_view = smart::data::VectorView<const double>::contiguous(nullptr, {0});
    require(smart::linalg::dot(empty_view, empty_view,
                smart::NumericalOptions{smart::NumericalPolicy::Reproducible}) == 0.0,
            "empty dot did not return zero");
    require(smart::linalg::norm(empty_view,
                smart::NumericalOptions{smart::NumericalPolicy::Accurate}) == 0.0,
            "empty norm did not return zero");
}


void test_reproducible_pointwise_execution()
{
    ConfigGuard guard;
    const std::size_t vector_size = 262144;
    std::vector<double> x_storage(vector_size), initial_y(vector_size);
    for (std::size_t index = 0; index < vector_size; ++index)
    {
        x_storage[index] = std::sin(static_cast<double>(index) * 0.003) + 0.25;
        initial_y[index] = std::cos(static_cast<double>(index) * 0.002) - 0.5;
    }
    const auto x = smart::data::VectorView<const double>::contiguous(
        x_storage.data(), {vector_size});

    constexpr std::size_t rows = 512;
    constexpr std::size_t columns = 513;
    std::vector<double> stencil_input(rows * columns);
    for (std::size_t row = 0; row < rows; ++row)
        for (std::size_t column = 0; column < columns; ++column)
            stencil_input[row * columns + column] =
                2.0 + std::sin(static_cast<double>(row) * 0.01)
                    + std::cos(static_cast<double>(column) * 0.02);
    const auto stencil_in = smart::data::MatrixView<const double>::contiguous(
        stencil_input.data(), {rows, columns});
    const smart::scientific::Stencil2DCoefficients<double> coefficients{
        0.5, 0.125, 0.125, 0.125, 0.125};

    std::vector<smart::ExecutionEngineType> engines{
        smart::ExecutionEngineType::ThreadPool,
        smart::ExecutionEngineType::StaticThread};
#if SMARTPARALLEL_HAS_TBB
    engines.push_back(smart::ExecutionEngineType::OneTbb);
#endif

    for (auto policy : {smart::NumericalPolicy::Reproducible,
                        smart::NumericalPolicy::Accurate})
    {
        std::vector<double> expected_axpy;
        std::vector<double> expected_stencil;
        bool have_expected = false;
        for (auto engine : engines)
        {
            for (std::size_t workers : {std::size_t{1}, std::size_t{2},
                                        std::size_t{3}, std::size_t{4},
                                        std::size_t{8}})
            {
                configure(engine, workers);
                std::vector<double> y_storage = initial_y;
                auto y = smart::data::VectorView<double>::contiguous(
                    y_storage.data(), {vector_size});
                smart::linalg::axpy(
                    y, 0.375, x, smart::NumericalOptions{policy});
                const auto axpy_report = smart::global_last_numerical_execution_report();
                require(axpy_report.evaluation_order
                            == smart::detail::EvaluationOrder::CanonicalPointwise,
                        "AXPY did not authenticate canonical pointwise evaluation");
                require(axpy_report.accumulation
                            == smart::detail::AccumulationMethod::FixedPointwiseExpression,
                        "AXPY reported a reduction accumulation method");
                require(std::string(axpy_report.canonical_plan)
                            == smart::detail::canonical_pointwise_plan_v1,
                        "AXPY pointwise plan identity mismatch");
                require(axpy_report.route_authenticated,
                        "AXPY pointwise route was not authenticated");
                if (workers > 1)
                    require(axpy_report.parallel,
                            "large deterministic AXPY unexpectedly ran sequentially");

                std::vector<double> stencil_output(rows * columns, -1.0);
                auto stencil_out = smart::data::MatrixView<double>::contiguous(
                    stencil_output.data(), {rows, columns});
                smart::scientific::stencil_2d(
                    stencil_in, stencil_out, coefficients,
                    smart::NumericalOptions{policy});
                const auto stencil_report = smart::global_last_numerical_execution_report();
                require(stencil_report.evaluation_order
                            == smart::detail::EvaluationOrder::CanonicalPointwise,
                        "stencil did not authenticate canonical pointwise evaluation");
                require(stencil_report.accumulation
                            == smart::detail::AccumulationMethod::FixedPointwiseExpression,
                        "stencil reported a reduction accumulation method");
                require(std::string(stencil_report.canonical_plan)
                            == smart::detail::canonical_pointwise_2d_plan_v1,
                        "stencil pointwise plan identity mismatch");
                require(stencil_report.route_authenticated,
                        "stencil pointwise route was not authenticated");
                if (workers > 1)
                    require(stencil_report.parallel,
                            "large deterministic stencil unexpectedly ran sequentially");

                if (!have_expected)
                {
                    expected_axpy = y_storage;
                    expected_stencil = stencil_output;
                    have_expected = true;
                }
                else
                {
                    require(y_storage.size() == expected_axpy.size(),
                            "AXPY result size changed");
                    require(stencil_output.size() == expected_stencil.size(),
                            "stencil result size changed");
                    for (std::size_t index = 0; index < y_storage.size(); ++index)
                        require(bits(y_storage[index]) == bits(expected_axpy[index]),
                                "AXPY changed across scheduler or worker count");
                    for (std::size_t index = 0; index < stencil_output.size(); ++index)
                        require(bits(stencil_output[index]) == bits(expected_stencil[index]),
                                "stencil changed across scheduler or worker count");
                }
            }
        }
    }
}

void test_float_operations()
{
    std::vector<float> x_storage(24, 0.0f), y_storage(32, 0.0f);
    for (std::size_t index = 0; index < 8; ++index)
    {
        x_storage[index * 3] = static_cast<float>(index + 1);
        y_storage[index * 4] = static_cast<float>(2 * index + 1);
    }
    const auto x = smart::data::VectorView<const float>(x_storage.data(), {8}, {3});
    auto y = smart::data::VectorView<float>(y_storage.data(), {8}, {4});
    smart::linalg::axpy(y, -0.25f, x,
        smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    for (std::size_t index = 0; index < 8; ++index)
        require(y(index) == static_cast<float>(2 * index + 1) - 0.25f * static_cast<float>(index + 1),
                "float strided AXPY failed");

    std::vector<float> ones_storage(16, 0.0f);
    for (std::size_t index = 0; index < 8; ++index) ones_storage[index * 2] = 1.0f;
    const auto ones = smart::data::VectorView<const float>(ones_storage.data(), {8}, {2});
    const float dot = smart::linalg::dot(
        x, ones, smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    require(std::abs(dot - 36.0f) <= 2.0f * std::numeric_limits<float>::epsilon() * 36.0f,
            "float Accurate dot failed");
    const float norm = smart::linalg::norm(
        x, smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    require(std::abs(norm - std::sqrt(204.0f)) <= 4.0f * std::numeric_limits<float>::epsilon() * norm,
            "float Accurate norm failed");

    std::vector<float> stencil_input(25), stencil_output(25, 0.0f);
    std::iota(stencil_input.begin(), stencil_input.end(), 0.0f);
    const smart::scientific::Stencil2DCoefficients<float> coefficients{
        0.5f, 0.125f, 0.125f, 0.125f, 0.125f};
    smart::scientific::stencil_2d(
        smart::data::MatrixView<const float>::contiguous(stencil_input.data(), {5, 5}),
        smart::data::MatrixView<float>::contiguous(stencil_output.data(), {5, 5}),
        coefficients, smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
    require(stencil_output[12] == 12.0f, "float stencil center failed");
}

void reference_stencil(const smart::data::MatrixView<const double>& input,
                       smart::data::MatrixView<double> output)
{
    const smart::scientific::Stencil2DCoefficients<double> c{0.5, 0.125, 0.125, 0.125, 0.125};
    for (std::size_t row = 0; row < input.extent(0); ++row)
        for (std::size_t col = 0; col < input.extent(1); ++col)
            if (row == 0 || col == 0 || row + 1 == input.extent(0) || col + 1 == input.extent(1))
                output(row, col) = input(row, col);
            else
                output(row, col) = c.center * input(row, col)
                    + c.north * input(row - 1, col)
                    + c.south * input(row + 1, col)
                    + c.west * input(row, col - 1)
                    + c.east * input(row, col + 1);
}

void test_stencil_and_heat()
{
    constexpr std::size_t rows = 9, columns = 7, stride = 11;
    std::vector<double> input_storage(rows * stride, -5.0);
    std::vector<double> output_storage(rows * stride, -9.0);
    std::vector<double> reference_storage(rows * stride, -9.0);
    auto input_rw = smart::data::MatrixView<double>(input_storage.data(), {rows, columns}, {stride, 1});
    for (std::size_t row = 0; row < rows; ++row)
        for (std::size_t col = 0; col < columns; ++col)
            input_rw(row, col) = static_cast<double>(row * 10 + col);
    smart::data::MatrixView<const double> input(input_rw);
    auto output = smart::data::MatrixView<double>(output_storage.data(), {rows, columns}, {stride, 1});
    auto reference = smart::data::MatrixView<double>(reference_storage.data(), {rows, columns}, {stride, 1});
    const smart::scientific::Stencil2DCoefficients<double> coefficients{0.5, 0.125, 0.125, 0.125, 0.125};
    reference_stencil(input, reference);
    smart::scientific::stencil_2d(input, output, coefficients,
        smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
    for (std::size_t row = 0; row < rows; ++row)
        for (std::size_t col = 0; col < columns; ++col)
            require(bits(output(row, col)) == bits(reference(row, col)),
                    "padded stencil differed from reference");

    // Exercise the validated raw-pointer kernel with non-unit column strides
    // for both input and output. The public View contract remains checked at
    // construction and operation entry; the inner loop must preserve the same
    // logical mapping without calling the checked indexer per element.
    constexpr std::size_t exotic_rows = 7;
    constexpr std::size_t exotic_columns = 6;
    constexpr std::size_t input_row_stride = 29;
    constexpr std::size_t input_column_stride = 3;
    constexpr std::size_t output_row_stride = 37;
    constexpr std::size_t output_column_stride = 4;
    std::vector<double> exotic_input_storage(
        (exotic_rows - 1) * input_row_stride
            + (exotic_columns - 1) * input_column_stride + 1,
        -5.0);
    std::vector<double> exotic_output_storage(
        (exotic_rows - 1) * output_row_stride
            + (exotic_columns - 1) * output_column_stride + 1,
        -9.0);
    std::vector<double> exotic_reference_storage = exotic_output_storage;
    auto exotic_input_rw = smart::data::MatrixView<double>(
        exotic_input_storage.data(), {exotic_rows, exotic_columns},
        {input_row_stride, input_column_stride});
    for (std::size_t row = 0; row < exotic_rows; ++row)
        for (std::size_t col = 0; col < exotic_columns; ++col)
            exotic_input_rw(row, col) = 0.25 + static_cast<double>(row * 17 + col * 5);
    smart::data::MatrixView<const double> exotic_input(exotic_input_rw);
    auto exotic_output = smart::data::MatrixView<double>(
        exotic_output_storage.data(), {exotic_rows, exotic_columns},
        {output_row_stride, output_column_stride});
    auto exotic_reference = smart::data::MatrixView<double>(
        exotic_reference_storage.data(), {exotic_rows, exotic_columns},
        {output_row_stride, output_column_stride});
    reference_stencil(exotic_input, exotic_reference);
    smart::scientific::stencil_2d(
        exotic_input, exotic_output, coefficients,
        smart::NumericalOptions{smart::NumericalPolicy::Accurate});
    for (std::size_t row = 0; row < exotic_rows; ++row)
        for (std::size_t col = 0; col < exotic_columns; ++col)
            require(bits(exotic_output(row, col)) == bits(exotic_reference(row, col)),
                    "non-unit-column-stride stencil differed from reference");

    std::vector<double> tiny_in{7.0}, tiny_out{0.0};
    smart::scientific::stencil_2d(
        smart::data::MatrixView<const double>::contiguous(tiny_in.data(), {1, 1}),
        smart::data::MatrixView<double>::contiguous(tiny_out.data(), {1, 1}), coefficients);
    require(tiny_out[0] == 7.0, "1x1 boundary copy failed");

    for (const auto& shape : {std::pair<std::size_t, std::size_t>{0, 0},
                              {1, 5}, {5, 1}, {2, 2}})
    {
        const std::size_t count = shape.first * shape.second;
        std::vector<double> small_input(count, 3.0), small_output(count, -1.0);
        smart::scientific::stencil_2d(
            smart::data::MatrixView<const double>::contiguous(
                count == 0 ? nullptr : small_input.data(), {shape.first, shape.second}),
            smart::data::MatrixView<double>::contiguous(
                count == 0 ? nullptr : small_output.data(), {shape.first, shape.second}),
            coefficients, smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
        require(small_output == small_input, "tiny stencil boundary-copy contract failed");
    }

    bool in_place_stencil_threw = false;
    try
    {
        auto same = smart::data::MatrixView<double>::contiguous(input_storage.data(), {rows, columns});
        smart::scientific::stencil_2d(
            smart::data::MatrixView<const double>(same), same, coefficients);
    }
    catch (const std::invalid_argument&) { in_place_stencil_threw = true; }
    require(in_place_stencil_threw, "in-place stencil overlap was not rejected");

    bool stencil_alias_threw = false;
    try
    {
        std::vector<double> separate_input(9, 1.0), aliased_output(3, 0.0);
        smart::scientific::stencil_2d(
            smart::data::MatrixView<const double>::contiguous(separate_input.data(), {3, 3}),
            smart::data::MatrixView<double>(aliased_output.data(), {3, 3}, {0, 1}),
            coefficients);
    }
    catch (const std::invalid_argument&) { stencil_alias_threw = true; }
    require(stencil_alias_threw, "stencil non-unique output mapping was not rejected");

    // Five integration iterations, independently repeated with the reference.
    std::vector<double> a(15 * 17, 10.0), b(a.size(), 0.0), r = a, scratch(a.size(), 0.0);
    for (std::size_t col = 0; col < 17; ++col) a[col] = a[14 * 17 + col] = 100.0;
    for (std::size_t row = 0; row < 15; ++row) a[row * 17] = a[row * 17 + 16] = 100.0;
    r = a; b = a; scratch = r;
    for (int iteration = 0; iteration < 5; ++iteration)
    {
        smart::scientific::stencil_2d(
            smart::data::MatrixView<const double>::contiguous(a.data(), {15, 17}),
            smart::data::MatrixView<double>::contiguous(b.data(), {15, 17}), coefficients,
            smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
        a.swap(b);
        reference_stencil(
            smart::data::MatrixView<const double>::contiguous(r.data(), {15, 17}),
            smart::data::MatrixView<double>::contiguous(scratch.data(), {15, 17}));
        r.swap(scratch);
    }
    require(a == r, "heat diffusion integration differed from reference");
}
}

int main()
{
    try
    {
        test_views();
        test_reproducible_reductions();
        test_special_values();
        test_linalg();
        test_reproducible_pointwise_execution();
        test_float_operations();
        test_stencil_and_heat();
        std::cout << "SmartParallel v1.6 scientific foundations validation passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "v1.6 validation failed: " << error.what() << '\n';
        return 1;
    }
}
