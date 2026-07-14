#include <cassert>

#include <smart/model/hardware_prediction.hpp>

int main()
{
    using namespace smart;

    HardwareCharacteristics hw;
    hw.logical_threads = 16;
    hw.physical_cores = 8;
    hw.numa_nodes = 2;
    hw.page_size = 4096;
    hw.cache_line_size = 64;
    hw.l2_cache_size = 512 * 1024;
    hw.l3_cache_size = 16 * 1024 * 1024;
    hw.cache_info_available = true;
    hw.numa_info_available = true;

    WorkloadAnalysis local;
    local.working_set_bytes = 2 * 1024 * 1024;
    DimensionAnalysis contiguous;
    contiguous.contiguous = true;
    contiguous.contiguous_known = true;
    contiguous.random_access = false;
    contiguous.random_access_known = true;
    contiguous.storage_kind = StorageKind::Contiguous;
    local.structural.dimensions.push_back(contiguous);

    WorkloadAnalysis remote = local;
    remote.working_set_bytes = 128 * 1024 * 1024;
    remote.structural.dimensions[0].contiguous = false;
    remote.structural.dimensions[0].random_access = true;
    remote.structural.dimensions[0].storage_kind = StorageKind::NodeBased;

    const auto local_context = hardware_prediction_context(local, hw, 8);
    const auto remote_context = hardware_prediction_context(remote, hw, 16);

    assert(local_context.locality_score > remote_context.locality_score);
    assert(remote_context.l3_pressure > local_context.l3_pressure);
    assert(remote_context.numa_pressure > 0.0);
    assert(remote_context.bandwidth_parallelism_limit <
           local_context.bandwidth_parallelism_limit);
    assert(remote_context.smt_ratio >= 2.0);
    return 0;
}
