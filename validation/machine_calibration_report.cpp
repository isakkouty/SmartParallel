#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include <smart/decision/execution_plan.hpp>
#include <smart/decision/runtime_calibration.hpp>

int main()
{
    try
    {
        const std::filesystem::path output_dir =
            std::filesystem::current_path() / "validation" / "output";
        std::filesystem::create_directories(output_dir);
        const std::filesystem::path output_path =
            output_dir / "machine_calibration.csv";

        std::ofstream csv(output_path);
        if (!csv)
            throw std::runtime_error("Unable to create machine calibration CSV");

        csv << std::setprecision(12);
        csv
            << "engine,workers,base_overhead_ms,log_slope_ms,"
            << "overhead_relative_uncertainty,cheap_small_speedup,"
            << "cheap_large_speedup,compute_speedup,streaming_speedup,"
            << "cheap_small_confidence,cheap_large_confidence,"
            << "compute_confidence,streaming_confidence,"
            << "speedup_relative_uncertainty,"
            << "overhead_1_chunk_ms,overhead_8_chunks_ms,"
            << "overhead_64_chunks_ms,overhead_512_chunks_ms\n";

        const smart::BackendRuntimeCalibration& calibration =
            smart::backend_runtime_calibration();

        for (const smart::BackendRuntimeCalibrationPoint& point :
             calibration.points)
        {
            const auto overhead = [&](std::size_t chunks)
            {
                // Passing iterations == chunks and chunk_size == 1 makes the
                // public helper evaluate the fitted curve at that task count.
                return smart::calibrated_backend_overhead_ms(
                    point.engine,
                    chunks,
                    point.workers,
                    1);
            };

            csv
                << smart::engine_name(point.engine) << ','
                << point.workers << ','
                << point.base_overhead_ms << ','
                << point.log_slope_ms << ','
                << point.overhead_relative_uncertainty << ','
                << point.cheap_small_speedup << ','
                << point.cheap_large_speedup << ','
                << point.compute_speedup << ','
                << point.streaming_speedup << ','
                << point.speedup_relative_uncertainty << ','
                << overhead(1) << ','
                << overhead(8) << ','
                << overhead(64) << ','
                << overhead(512) << '\n';
        }

        std::cout
            << "==== SmartParallel Machine Calibration Report ====\n"
            << "Measured calibration: "
            << (calibration.measured ? "yes" : "no") << '\n'
            << "Calibration points : " << calibration.points.size() << '\n'
            << "Results written to : " << output_path.string() << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Machine calibration report failed: "
                  << error.what() << '\n';
        return 1;
    }
}
