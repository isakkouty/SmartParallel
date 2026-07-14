#include <smart/decision/runtime_calibration.hpp>
#include <smart/hardware/hardware.hpp>

#include <cassert>
#include <cmath>
#include <cstddef>

int main()
{
    const smart::BackendRuntimeCalibration& calibration =
        smart::backend_runtime_calibration();

    if (smart::hardware_threads() <= 1)
    {
        return 0;
    }

    assert(calibration.measured);
    assert(!calibration.points.empty());

    for (const smart::BackendRuntimeCalibrationPoint& point : calibration.points)
    {
        assert(point.workers >= 2);
        assert(std::isfinite(point.base_overhead_ms));
        assert(point.base_overhead_ms >= 0.0);
        assert(std::isfinite(point.log_slope_ms));
        assert(point.log_slope_ms >= 0.0);
        assert(std::isfinite(point.compute_speedup));
        assert(point.compute_speedup >= 1.0);
        assert(std::isfinite(point.streaming_speedup));
        assert(point.streaming_speedup >= 1.0);

        const double overhead = smart::calibrated_backend_overhead_ms(
            point.engine,
            100'000,
            point.workers,
            128);
        assert(std::isfinite(overhead));
        assert(overhead >= 0.0);

        const double compute_speedup = smart::calibrated_backend_speedup(
            point.engine,
            point.workers,
            smart::RuntimeWorkloadClass::ComputeLike);
        const double streaming_speedup = smart::calibrated_backend_speedup(
            point.engine,
            point.workers,
            smart::RuntimeWorkloadClass::StreamingLike);
        assert(compute_speedup >= 1.0);
        assert(streaming_speedup >= 1.0);
    }

    return 0;
}
