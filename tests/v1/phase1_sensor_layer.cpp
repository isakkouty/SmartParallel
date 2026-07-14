#include <smart/profiling/isolated_function_profile.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_builder.hpp>

#include <cassert>
#include <cmath>
#include <deque>
#include <vector>

namespace
{
    void cheap_work(int& value)
    {
        value += 1;
    }

    void variable_work(int& value)
    {
        const int iterations = (value % 7 == 0) ? 5000 : 20;
        volatile int sink = value;
        for (int i = 0; i < iterations; ++i)
        {
            sink = sink * 3 + i;
        }
        value = sink;
    }
}

int main()
{
    std::vector<int> values(1024, 1);
    const smart::Workload vector_workload =
        smart::WorkloadBuilder::container(values);

    assert(vector_workload.dimensions.size() == 1);
    assert(
        vector_workload.dimensions[0].storage_kind ==
        smart::StorageKind::Contiguous);
    assert(vector_workload.dimensions[0].contiguous_known);
    assert(vector_workload.dimensions[0].contiguous);
    assert(vector_workload.dimensions[0].random_access);
    assert(vector_workload.dimensions[0].stride_known);
    assert(vector_workload.dimensions[0].stride_bytes == sizeof(int));

    smart::WorkloadAnalyzer analyzer;
    const smart::WorkloadAnalysis vector_analysis =
        analyzer.analyze(vector_workload);

    assert(vector_analysis.structural.logical_iterations == values.size());
    assert(vector_analysis.structural.dimensionality == 1);
    assert(
        vector_analysis.structural.represented_input_bytes ==
        values.size() * sizeof(int));
    assert(vector_analysis.structural.unique_input_elements == values.size());
    assert(vector_analysis.structural.dimensions.size() == 1);
    assert(
        vector_analysis.structural.dimensions[0].reuse_factor == 1);

    std::vector<int> left(10, 1);
    std::vector<double> right(20, 2.0);
    const smart::Workload pair_workload =
        smart::WorkloadBuilder::pair_container(left, right);
    const smart::WorkloadAnalysis pair_analysis =
        analyzer.analyze(pair_workload);

    assert(pair_analysis.structural.logical_iterations == 200);
    assert(pair_analysis.structural.dimensionality == 2);
    assert(pair_analysis.structural.unique_input_elements == 30);
    assert(pair_analysis.structural.dimensions[0].reuse_factor == 20);
    assert(pair_analysis.structural.dimensions[1].reuse_factor == 10);

    std::deque<int> deque_values(64, 1);
    const smart::Workload deque_workload =
        smart::WorkloadBuilder::container(deque_values);
    assert(deque_workload.dimensions[0].random_access);
    assert(!deque_workload.dimensions[0].contiguous_known);
    assert(
        deque_workload.dimensions[0].storage_kind ==
        smart::StorageKind::Unknown);

    smart::FunctionProfiler::Config config;
    config.min_samples = 4;
    config.max_samples = 16;
    config.batch_size = 1;
    config.max_batch_size = 1024;
    config.max_callback_invocations = 4096;
    config.max_profile_time_ms = 20.0;
    config.target_batch_duration_ms = 0.02;

    const std::vector<int> original_values = values;
    const smart::FunctionProfile cheap_profile =
        smart::profile_container_on_copies(values, cheap_work, config);

    assert(cheap_profile.available);
    assert(values == original_values);
    assert(cheap_profile.chosen_batch_size >= 1);
    assert(cheap_profile.samples >= 1);
    assert(cheap_profile.callback_invocations >= cheap_profile.samples);
    assert(cheap_profile.trimmed_mean_ms_per_iteration >= 0.0);
    assert(cheap_profile.median_ms_per_iteration >= 0.0);
    assert(cheap_profile.coefficient_of_variation >= 0.0);

    std::vector<int> variable_values(512);
    for (std::size_t i = 0; i < variable_values.size(); ++i)
    {
        variable_values[i] = static_cast<int>(i);
    }

    const smart::FunctionProfile variable_profile =
        smart::profile_container_on_copies(
            variable_values,
            variable_work,
            config);

    assert(variable_profile.available);
    assert(variable_profile.p95_ms_per_iteration >= 0.0);
    assert(variable_profile.tail_ratio >= 0.0);
    assert(variable_profile.profiling_elapsed_ms >= 0.0);
    assert(
        variable_profile.metadata.source ==
        smart::ObservationSource::Sampled);

    return 0;
}
