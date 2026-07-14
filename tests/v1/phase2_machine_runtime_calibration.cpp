#include <smart/core/config.hpp>
#include <smart/decision/runtime_calibration.hpp>
#include <smart/hardware/hardware.hpp>

#include <cassert>
#include <cmath>

int main()
{
    smart::global_config().enable_machine_runtime_calibration = true;

    const smart::BackendRuntimeCalibration& calibration =
        smart::backend_runtime_calibration();

    if (smart::hardware_threads() <= 1)
    {
        return 0;
    }

    assert(calibration.measured);
    assert(!calibration.points.empty());

    assert(
        smart::calibrated_backend_overhead_ms(
            smart::ExecutionEngineType::ThreadPool,
            1000,
            smart::hardware_threads(),
            64) >= 0.0);
    assert(
        smart::calibrated_backend_overhead_ms(
            smart::ExecutionEngineType::StaticThread,
            1000,
            smart::hardware_threads(),
            0) >= 0.0);
    assert(
        smart::calibrated_backend_overhead_ms(
            smart::ExecutionEngineType::OneTbb,
            1000,
            smart::hardware_threads(),
            64) >= 0.0);

    return 0;
}
