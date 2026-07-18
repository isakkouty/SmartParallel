#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>

#include <smart/core/config.hpp>
#include <smart/decision/decision_report.hpp>
#include <smart/decision/decision_report_printer.hpp>
#include <smart/execution/parallel.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    struct Case
    {
        const char* name;
        int width;
        int height;
        int repetitions;
    };

    struct Result
    {
        std::string case_name;
        int width = 0;
        int height = 0;
        std::size_t pixels = 0;
        double sequential_ms = 0.0;
        double opencv_parallel_ms = 0.0;
        double smartparallel_ms = 0.0;
        double opencv_threshold_ms = 0.0;
        std::string smart_plan;
        bool correct = false;
    };

    template <typename Function>
    double median_runtime_ms(int repetitions, Function&& function)
    {
        std::vector<double> samples;
        samples.reserve(static_cast<std::size_t>(repetitions));

        function(); // warm-up
        for (int i = 0; i < repetitions; ++i)
        {
            const auto begin = Clock::now();
            function();
            const auto end = Clock::now();
            samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        }

        std::sort(samples.begin(), samples.end());
        const std::size_t middle = samples.size() / 2;
        if ((samples.size() % 2) == 0)
            return (samples[middle - 1] + samples[middle]) * 0.5;
        return samples[middle];
    }

    void threshold_sequential(const cv::Mat& source, cv::Mat& destination, std::uint8_t threshold)
    {
        const auto* input = source.ptr<std::uint8_t>();
        auto* output = destination.ptr<std::uint8_t>();
        const std::size_t total = source.total();
        for (std::size_t i = 0; i < total; ++i)
            output[i] = input[i] > threshold ? 255 : 0;
    }

    void threshold_opencv_parallel(const cv::Mat& source, cv::Mat& destination, std::uint8_t threshold)
    {
        const auto* input = source.ptr<std::uint8_t>();
        auto* output = destination.ptr<std::uint8_t>();
        const int total = static_cast<int>(source.total());

        cv::parallel_for_(cv::Range(0, total), [&](const cv::Range& range)
        {
            for (int i = range.start; i < range.end; ++i)
                output[i] = input[i] > threshold ? 255 : 0;
        });
    }

    void threshold_smartparallel(const cv::Mat& source, cv::Mat& destination, std::uint8_t threshold)
    {
        const auto* input = source.ptr<std::uint8_t>();
        auto* output = destination.ptr<std::uint8_t>();
        const std::size_t total = source.total();

        smart::parallel_for(0, total, [&](std::size_t i)
        {
            output[i] = input[i] > threshold ? 255 : 0;
        });
    }

    std::string selected_plan()
    {
        const auto& plan = smart::global_last_decision_report().plan;
        if (!plan.parallel || plan.strategy == smart::ExecutionStrategy::Sequential)
            return "Sequential";

        return std::string(smart::engine_name(plan.engine)) +
            "/" + smart::execution_strategy_name(plan.strategy) +
            "/w" + std::to_string(plan.job_count) +
            "/c" + std::to_string(plan.chunk_size);
    }

    bool equal_images(const cv::Mat& a, const cv::Mat& b)
    {
        return a.size() == b.size() && a.type() == b.type() &&
            cv::countNonZero(a != b) == 0;
    }

    void write_csv(const std::filesystem::path& path, const std::vector<Result>& results)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path);
        if (!output)
            throw std::runtime_error("Could not open output CSV: " + path.string());

        output << "case,width,height,pixels,sequential_ms,opencv_parallel_ms,smartparallel_ms,"
                  "opencv_threshold_ms,smart_vs_sequential_speedup,smart_vs_opencv_parallel_speedup,"
                  "smart_plan,correct\n";
        output << std::fixed << std::setprecision(6);
        for (const Result& result : results)
        {
            output << result.case_name << ',' << result.width << ',' << result.height << ','
                   << result.pixels << ',' << result.sequential_ms << ','
                   << result.opencv_parallel_ms << ',' << result.smartparallel_ms << ','
                   << result.opencv_threshold_ms << ','
                   << result.sequential_ms / result.smartparallel_ms << ','
                   << result.opencv_parallel_ms / result.smartparallel_ms << ','
                   << result.smart_plan << ',' << (result.correct ? "true" : "false") << '\n';
        }
    }
}

int main(int argc, char** argv)
{
    try
    {
        const std::filesystem::path output_path = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("validation/output/opencv_test1_threshold.csv");

        smart::global_config().enable_experience = false;
        smart::global_config().enable_utility_model_runtime = false;
        smart::global_config().execution_engine = smart::ExecutionEngineType::Auto;

        const std::vector<Case> cases{
            {"small_64x64", 64, 64, 101},
            {"medium_640x480", 640, 480, 51},
            {"large_1920x1080", 1920, 1080, 31},
            {"xlarge_3840x2160", 3840, 2160, 15},
        };

        cv::RNG random(0x5A17);
        std::vector<Result> results;
        bool all_correct = true;

        std::cout << "==== SmartParallel OpenCV Test 1: binary threshold ====\n";
        std::cout << "OpenCV version: " << CV_VERSION << "\n\n";

        for (const Case& test_case : cases)
        {
            cv::Mat source(test_case.height, test_case.width, CV_8UC1);
            random.fill(source, cv::RNG::UNIFORM, 0, 256);
            CV_Assert(source.isContinuous());

            cv::Mat sequential(source.size(), source.type());
            cv::Mat opencv_parallel(source.size(), source.type());
            cv::Mat smartparallel(source.size(), source.type());
            cv::Mat opencv_reference(source.size(), source.type());
            constexpr std::uint8_t threshold = 127;

            const double sequential_ms = median_runtime_ms(test_case.repetitions, [&]
            {
                threshold_sequential(source, sequential, threshold);
            });
            const double opencv_parallel_ms = median_runtime_ms(test_case.repetitions, [&]
            {
                threshold_opencv_parallel(source, opencv_parallel, threshold);
            });
            const double smartparallel_ms = median_runtime_ms(test_case.repetitions, [&]
            {
                threshold_smartparallel(source, smartparallel, threshold);
            });
            const std::string plan = selected_plan();
            const double opencv_threshold_ms = median_runtime_ms(test_case.repetitions, [&]
            {
                cv::threshold(source, opencv_reference, threshold, 255, cv::THRESH_BINARY);
            });

            const bool correct = equal_images(sequential, opencv_reference) &&
                equal_images(opencv_parallel, opencv_reference) &&
                equal_images(smartparallel, opencv_reference);
            all_correct = all_correct && correct;

            Result result;
            result.case_name = test_case.name;
            result.width = test_case.width;
            result.height = test_case.height;
            result.pixels = source.total();
            result.sequential_ms = sequential_ms;
            result.opencv_parallel_ms = opencv_parallel_ms;
            result.smartparallel_ms = smartparallel_ms;
            result.opencv_threshold_ms = opencv_threshold_ms;
            result.smart_plan = plan;
            result.correct = correct;
            results.push_back(result);

            std::cout << std::left << std::setw(20) << test_case.name
                      << " pixels=" << std::setw(9) << source.total()
                      << " smart=" << std::setw(34) << plan
                      << " correct=" << (correct ? "yes" : "NO") << '\n';
            std::cout << std::right << std::fixed << std::setprecision(4)
                      << "  sequential=" << sequential_ms << " ms"
                      << " | cv::parallel_for_=" << opencv_parallel_ms << " ms"
                      << " | SmartParallel=" << smartparallel_ms << " ms"
                      << " | cv::threshold=" << opencv_threshold_ms << " ms\n"
                      << "  Smart speedup vs sequential: " << sequential_ms / smartparallel_ms << "x"
                      << " | vs cv::parallel_for_: " << opencv_parallel_ms / smartparallel_ms << "x\n\n";
        }

        write_csv(output_path, results);
        std::cout << "CSV written to: " << output_path.string() << '\n';
        std::cout << "Correctness: " << (all_correct ? "PASS" : "FAIL") << '\n';
        return all_correct ? 0 : 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "OpenCV Test 1 failed: " << error.what() << '\n';
        return 1;
    }
}
