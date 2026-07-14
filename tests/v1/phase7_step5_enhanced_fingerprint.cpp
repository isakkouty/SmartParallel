#include <cassert>

#include <smart/workload/fingerprint.hpp>
#include <smart/workload/fingerprint_similarity.hpp>

int main()
{
    using namespace smart;

    Workload contiguous;
    contiguous.kind = WorkloadKind::Container;
    contiguous.iterations = 65536;
    Dimension a;
    a.size = 65536;
    a.object_size = 8;
    a.storage_kind = StorageKind::Contiguous;
    a.contiguous = true;
    a.contiguous_known = true;
    a.random_access = false;
    a.random_access_known = true;
    a.stride_bytes = 8;
    a.stride_known = true;
    contiguous.dimensions.push_back(a);

    Workload random = contiguous;
    random.dimensions[0].storage_kind = StorageKind::NodeBased;
    random.dimensions[0].contiguous = false;
    random.dimensions[0].random_access = true;
    random.dimensions[0].stride_bytes = 4096;

    const WorkloadFingerprint left = fingerprint(contiguous);
    const WorkloadFingerprint same = fingerprint(contiguous);
    const WorkloadFingerprint right = fingerprint(random);

    assert(left.value == same.value);
    assert(left.access_pattern_bucket == same.access_pattern_bucket);
    assert(left.access_pattern_bucket != right.access_pattern_bucket);
    assert(left.stride_bucket != right.stride_bucket);
    assert(left.cache_pressure_bucket != 0);
    assert(left.topology_bucket != 0);

    const double identical = fingerprint_similarity(left, same);
    const double different = fingerprint_similarity(left, right);
    assert(identical > different);
    assert(different < 0.90);
    return 0;
}
