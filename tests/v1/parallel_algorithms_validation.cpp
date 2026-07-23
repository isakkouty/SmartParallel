#include <smart/execution/algorithms.hpp>
#include <smart/execution/runtime_capabilities.hpp>
#include <smart/profiling/function_profile_cache.hpp>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct ConfigGuard
{
    smart::Config saved = smart::global_config();
    ~ConfigGuard() { smart::global_config() = saved; }
};

void configure_for_validation(smart::ExecutionEngineType engine)
{
    auto& config = smart::global_config();
    config.execution_engine = engine;
    config.enable_experience = false;
    config.enable_experience_ranking = false;
    config.enable_online_exploration = false;
    config.enable_parallel_for_backend_calibration = false;
    config.enable_parallel_for_profile_cache = true;
    config.enable_parallel_for_auto_profiling = true;
    config.enable_parallel_for_cached_sequential_fast_path = false;
    config.enable_parallel_for_tiny_work_bypass = false;
    config.parallel_for_minimum_predicted_speedup = 0.0;
    config.parallel_for_estimated_overhead_ms = 0.0;
    config.small_workload_iteration_threshold = 0;
    config.cheap_workload_sequential_threshold = 0;
    config.enable_nested_execution_session = true;
    config.nested_root_concurrency_budget = 4;
    config.nested_min_iterations_per_worker = 1;
    config.nested_min_parallel_work_ms = 0.0;
    config.nested_plan_hysteresis = 1.0;
    ++config.parallel_for_policy_generation;
    smart::global_function_profile_cache().clear();
}

void test_empty_range_contracts()
{
    std::vector<int> values;
    std::vector<int> output;
    smart::parallel_for_each(values.begin(), values.end(), [](int&) {});
    require(smart::parallel_transform(values.begin(), values.end(), output.begin(), [](int v) {
                return v + 1;
            }) == output.begin(),
            "empty transform returned wrong output iterator");
    require(smart::parallel_copy(values.begin(), values.end(), output.begin()) == output.begin(),
            "empty copy returned wrong output iterator");
    smart::parallel_fill(values.begin(), values.end(), 7);
    smart::parallel_generate(values.begin(), values.end(), [](std::size_t i) { return i; });
    require(smart::parallel_reduce(values.begin(), values.end(), 42) == 42,
            "empty reduce did not preserve init");
    require(smart::parallel_transform_reduce(
                values.begin(), values.end(), 9, std::plus<>{}, [](int v) { return v; }) == 9,
            "empty transform_reduce did not preserve init");
    require(smart::parallel_count(values.begin(), values.end(), 1) == 0,
            "empty count was nonzero");
    require(smart::parallel_count_if(values.begin(), values.end(), [](int) { return true; }) == 0,
            "empty count_if was nonzero");
    require(!smart::parallel_any_of(values.begin(), values.end(), [](int) { return true; }),
            "empty any_of must be false");
    require(smart::parallel_all_of(values.begin(), values.end(), [](int) { return false; }),
            "empty all_of must be true");
    require(smart::parallel_none_of(values.begin(), values.end(), [](int) { return true; }),
            "empty none_of must be true");
    require(smart::parallel_find(values.begin(), values.end(), 1) == values.end(),
            "empty find must return end");
    require(smart::parallel_find_if(values.begin(), values.end(), [](int) { return true; })
                == values.end(),
            "empty find_if must return end");
}

void test_single_element_and_invalid_range_contracts()
{
    std::vector<int> values{4};
    std::vector<int> output(1, 0);

    smart::parallel_for_each(values.begin(), values.end(), [](int& value) { value += 1; });
    require(values.front() == 5, "single-element for_each failed");
    require(smart::parallel_transform(
                values.begin(), values.end(), output.begin(), [](int value) { return value * 2; })
                == output.end(),
            "single-element transform returned wrong end");
    require(output.front() == 10, "single-element transform failed");
    require(smart::parallel_reduce(values.begin(), values.end(), 3) == 8,
            "single-element reduce failed");
    require(smart::parallel_transform_reduce(
                values.begin(), values.end(), 2, std::plus<>{}, [](int value) { return value * 3; })
                == 17,
            "single-element transform_reduce failed");
    require(smart::parallel_count(values.begin(), values.end(), 5) == 1,
            "single-element count failed");
    require(smart::parallel_any_of(values.begin(), values.end(), [](int value) { return value == 5; }),
            "single-element any_of failed");
    require(smart::parallel_find(values.begin(), values.end(), 5) == values.begin(),
            "single-element find failed");

    bool reversed_threw = false;
    try
    {
        smart::parallel_for_each(values.end(), values.begin(), [](int&) {});
    }
    catch (const std::invalid_argument&)
    {
        reversed_threw = true;
    }
    require(reversed_threw, "reversed range did not throw invalid_argument");
}

void test_elementwise_algorithms()
{
    constexpr std::size_t count = 8192;
    std::vector<std::size_t> values(count);
    std::iota(values.begin(), values.end(), std::size_t{0});
    std::vector<std::atomic<unsigned>> visits(count);
    for (auto& visit : visits)
        visit.store(0, std::memory_order_relaxed);

    smart::parallel_for_each(values.begin(), values.end(), [&](std::size_t& value) {
        const std::size_t index = value;
        visits[index].fetch_add(1, std::memory_order_relaxed);
        value = index * 3 + 1;
    });
    for (std::size_t i = 0; i < count; ++i)
    {
        require(visits[i].load(std::memory_order_relaxed) == 1,
                "parallel_for_each duplicated or skipped an element");
        require(values[i] == i * 3 + 1, "parallel_for_each produced wrong output");
    }

    std::vector<std::size_t> unary(count, 0);
    auto unary_end = smart::parallel_transform(
        values.begin(), values.end(), unary.begin(), [](std::size_t value) { return value ^ 0x55u; });
    require(unary_end == unary.end(), "unary transform returned wrong end");

    std::vector<std::size_t> binary(count, 0);
    auto binary_end = smart::parallel_transform(
        values.begin(), values.end(), unary.begin(), binary.begin(), std::plus<>{});
    require(binary_end == binary.end(), "binary transform returned wrong end");
    for (std::size_t i = 0; i < count; ++i)
        require(binary[i] == values[i] + unary[i], "parallel_transform produced wrong output");

    smart::parallel_transform(values.begin(), values.end(), values.begin(), [](std::size_t value) {
        return value + 5;
    });
    for (std::size_t i = 0; i < count; ++i)
        require(values[i] == i * 3 + 6, "in-place transform failed");

    std::vector<std::size_t> copied(count, 0);
    require(smart::parallel_copy(values.begin(), values.end(), copied.begin()) == copied.end(),
            "parallel_copy returned wrong end");
    require(copied == values, "parallel_copy produced wrong output");

    smart::parallel_fill(copied.begin(), copied.end(), std::size_t{17});
    require(std::all_of(copied.begin(), copied.end(), [](std::size_t v) { return v == 17; }),
            "parallel_fill produced wrong output");

    smart::parallel_generate(copied.begin(), copied.end(), [](std::size_t index) {
        return index * index + 11;
    });
    for (std::size_t i = 0; i < count; ++i)
        require(copied[i] == i * i + 11, "parallel_generate produced wrong output");
}

void test_reduction_algorithms()
{
    std::vector<std::uint64_t> values(10000);
    std::iota(values.begin(), values.end(), std::uint64_t{1});
    const std::uint64_t expected =
        std::accumulate(values.begin(), values.end(), std::uint64_t{7});
    require(smart::parallel_reduce(values.begin(), values.end(), std::uint64_t{7}) == expected,
            "parallel_reduce produced wrong sum");

    const std::uint64_t expected_squares = std::accumulate(
        values.begin(), values.end(), std::uint64_t{3}, [](std::uint64_t sum, std::uint64_t value) {
            return sum + value * value;
        });
    require(smart::parallel_transform_reduce(
                values.begin(), values.end(), std::uint64_t{3}, std::plus<>{},
                [](std::uint64_t value) { return value * value; }) == expected_squares,
            "unary parallel_transform_reduce produced wrong result");

    std::vector<std::uint64_t> other(values.size());
    std::iota(other.begin(), other.end(), std::uint64_t{2});
    std::uint64_t expected_dot = 5;
    for (std::size_t i = 0; i < values.size(); ++i)
        expected_dot += values[i] * other[i];
    require(smart::parallel_transform_reduce(
                values.begin(), values.end(), other.begin(), std::uint64_t{5},
                std::plus<>{}, std::multiplies<>{}) == expected_dot,
            "binary parallel_transform_reduce produced wrong result");

    const std::vector<std::string> text{"a", "b", "c", "d", "e", "f"};
    const std::string joined = smart::parallel_reduce(
        text.begin(), text.end(), std::string{"prefix:"},
        [](std::string left, const std::string& right) { return left + right; });
    require(joined == "prefix:abcdef", "parallel_reduce did not preserve chunk order");
}

void test_count_predicate_and_search_algorithms()
{
    std::vector<int> values(12000);
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = static_cast<int>(i % 37);
    values[321] = -7;
    values[9876] = -7;

    require(smart::parallel_count(values.begin(), values.end(), -7) == 2,
            "parallel_count produced wrong result");
    const std::size_t expected_even = static_cast<std::size_t>(
        std::count_if(values.begin(), values.end(), [](int value) { return value % 2 == 0; }));
    require(smart::parallel_count_if(values.begin(), values.end(), [](int value) {
                return value % 2 == 0;
            }) == expected_even,
            "parallel_count_if produced wrong result");

    require(smart::parallel_any_of(values.begin(), values.end(), [](int value) { return value < 0; }),
            "parallel_any_of missed a match");
    require(!smart::parallel_all_of(values.begin(), values.end(), [](int value) { return value >= 0; }),
            "parallel_all_of missed a failure");
    require(!smart::parallel_none_of(values.begin(), values.end(), [](int value) { return value < 0; }),
            "parallel_none_of missed a match");

    const auto found = smart::parallel_find(values.begin(), values.end(), -7);
    require(found == values.begin() + 321, "parallel_find did not return earliest match");
    const auto found_if = smart::parallel_find_if(
        values.begin(), values.end(), [](int value) { return value == 36; });
    require(found_if == values.begin() + 36, "parallel_find_if did not return earliest match");
    require(smart::parallel_find(values.begin(), values.end(), 999) == values.end(),
            "parallel_find did not return end for no match");
}

void test_exception_propagation()
{
    std::vector<int> values(4096, 1);
    std::vector<int> output(values.size(), 0);
    bool transform_threw = false;
    std::atomic<unsigned> transform_calls{0};
    try
    {
        smart::parallel_transform(values.begin(), values.end(), output.begin(), [&](int) {
            const unsigned current = transform_calls.fetch_add(1, std::memory_order_relaxed);
            if (current == 0)
                throw std::runtime_error("expected transform exception");
            return static_cast<int>(current);
        });
    }
    catch (const std::runtime_error&)
    {
        transform_threw = true;
    }
    require(transform_threw, "parallel_transform did not propagate exception");

    bool reduce_threw = false;
    try
    {
        (void)smart::parallel_reduce(values.begin(), values.end(), 0, [](int left, int right) {
            if (left > 32)
                throw std::runtime_error("expected reduce exception");
            return left + right;
        });
    }
    catch (const std::runtime_error&)
    {
        reduce_threw = true;
    }
    require(reduce_threw, "parallel_reduce did not propagate exception");

    bool predicate_threw = false;
    try
    {
        (void)smart::parallel_find_if(values.begin(), values.end(), [](int) -> bool {
            throw std::runtime_error("expected predicate exception");
        });
    }
    catch (const std::runtime_error&)
    {
        predicate_threw = true;
    }
    require(predicate_threw, "parallel_find_if did not propagate exception");
}

void test_nested_algorithms()
{
    constexpr std::size_t outer = 24;
    constexpr std::size_t inner = 2048;
    std::vector<std::uint64_t> results(outer, 0);
    smart::parallel_for(0, outer, [&](std::size_t row) {
        std::vector<std::uint64_t> values(inner);
        smart::parallel_generate(values.begin(), values.end(), [row](std::size_t index) {
            return static_cast<std::uint64_t>(row + index + 1);
        });
        results[row] = smart::parallel_transform_reduce(
            values.begin(), values.end(), std::uint64_t{0}, std::plus<>{},
            [](std::uint64_t value) { return value * 3; });
    });

    for (std::size_t row = 0; row < outer; ++row)
    {
        std::uint64_t expected = 0;
        for (std::size_t index = 0; index < inner; ++index)
            expected += static_cast<std::uint64_t>(row + index + 1) * 3;
        require(results[row] == expected, "nested v1.4 algorithm produced wrong result");
    }
}


void test_scheduler_approved_direct_sequential_path()
{
    auto& config = smart::global_config();
    config.execution_engine = smart::ExecutionEngineType::Auto;
    config.enable_experience = false;
    config.enable_experience_ranking = false;
    config.enable_online_exploration = false;
    config.enable_parallel_for_backend_calibration = false;
    config.enable_parallel_for_profile_cache = true;
    config.enable_parallel_for_auto_profiling = true;
    config.enable_parallel_for_cached_sequential_fast_path = true;
    config.enable_parallel_for_tiny_work_bypass = true;
    config.enable_nested_execution_session = true;
    config.enable_nested_root_online_telemetry = true;
    config.enable_root_analytical_cold_start = false;
    config.enable_root_pilot_cold_start = false;
    config.nested_root_concurrency_budget = 4;
    config.parallel_for_minimum_predicted_speedup = 100.0;
    config.parallel_for_sequential_fast_path_speedup_margin = 1.0;
    config.parallel_for_sequential_fast_path_min_observations = 2;
    config.parallel_for_profile_cache_min_hits = 1;
    ++config.parallel_for_policy_generation;
    smart::global_function_profile_cache().clear();

    std::vector<std::uint64_t> values(32768);
    std::iota(values.begin(), values.end(), std::uint64_t{0});
    const std::uint64_t expected_sum =
        std::accumulate(values.begin(), values.end(), std::uint64_t{11});

    std::uint64_t result = 0;
    for (std::size_t repetition = 0; repetition < 6; ++repetition)
        result = smart::parallel_reduce(values.begin(), values.end(), std::uint64_t{11});
    require(result == expected_sum, "direct sequential reduce produced wrong result");
    require(!smart::global_last_decision_report().plan.parallel,
            "direct sequential reduce did not retain a sequential plan");
    require(smart::global_last_parallel_for_profile_diagnostics().sequential_fast_path,
            "direct sequential reduce did not use the cached scheduler fast path");

    const std::size_t expected_count = static_cast<std::size_t>(
        std::count(values.begin(), values.end(), std::uint64_t{17}));
    std::size_t count = 0;
    for (std::size_t repetition = 0; repetition < 6; ++repetition)
        count = smart::parallel_count(values.begin(), values.end(), std::uint64_t{17});
    require(count == expected_count, "direct sequential count produced wrong result");
    require(!smart::global_last_decision_report().plan.parallel,
            "direct sequential count did not retain a sequential plan");

    auto found = values.end();
    for (std::size_t repetition = 0; repetition < 6; ++repetition)
        found = smart::parallel_find(values.begin(), values.end(), std::uint64_t{32767});
    require(found == values.end() - 1, "direct sequential find produced wrong result");
    require(!smart::global_last_decision_report().plan.parallel,
            "direct sequential find did not retain a sequential plan");
}

void run_backend(smart::ExecutionEngineType engine)
{
    configure_for_validation(engine);
    test_empty_range_contracts();
    test_single_element_and_invalid_range_contracts();
    test_elementwise_algorithms();
    test_reduction_algorithms();
    test_count_predicate_and_search_algorithms();
    test_exception_propagation();
    test_nested_algorithms();
}
} // namespace

int main()
{
    ConfigGuard guard;
    try
    {
        run_backend(smart::ExecutionEngineType::Auto);
        run_backend(smart::ExecutionEngineType::ThreadPool);
        run_backend(smart::ExecutionEngineType::StaticThread);
        if (smart::execution_backend_available(smart::ExecutionEngineType::OneTbb))
            run_backend(smart::ExecutionEngineType::OneTbb);
        test_scheduler_approved_direct_sequential_path();
        std::cout << "SmartParallel v1.4 parallel algorithm validation: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SmartParallel v1.4 parallel algorithm validation: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
