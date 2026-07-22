#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>
#include <smart/core/config.hpp>
#include <smart/decision/decision_report.hpp>
#include <smart/decision/decision_report_printer.hpp>
#include <smart/execution/parallel.hpp>
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
    double opencv_filter_ms = 0.0;
    std::string smart_plan;
    double max_manual_difference = 0.0;
    double max_opencv_difference = 0.0;
    bool correct = false;
};

constexpr int kernel_radius = 2;
constexpr int kernel_size = kernel_radius * 2 + 1;
constexpr std::array<float, kernel_size * kernel_size> kernel{
    1.0F,  4.0F, 6.0F, 4.0F,  1.0F,  4.0F,  16.0F, 24.0F, 16.0F, 4.0F, 6.0F, 24.0F, 36.0F,
    24.0F, 6.0F, 4.0F, 16.0F, 24.0F, 16.0F, 4.0F,  1.0F,  4.0F,  6.0F, 4.0F, 1.0F,
};
constexpr float kernel_scale = 1.0F / 256.0F;

template <typename Function>
double median_runtime_ms(int repetitions, Function&& function)
{
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repetitions));
    function();
    for (int i = 0; i < repetitions; ++i)
    {
        const auto begin = Clock::now();
        function();
        const auto end = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
    }
    std::sort(samples.begin(), samples.end());
    const std::size_t middle = samples.size() / 2;
    return (samples.size() % 2) == 0 ? (samples[middle - 1] + samples[middle]) * 0.5
                                     : samples[middle];
}

void convolve_linear_range(const cv::Mat& padded,
                           cv::Mat& destination,
                           std::size_t begin,
                           std::size_t end)
{
    const std::size_t width = static_cast<std::size_t>(destination.cols);
    for (std::size_t index = begin; index < end; ++index)
    {
        const int y = static_cast<int>(index / width);
        const int x = static_cast<int>(index % width);
        float sum = 0.0F;
        for (int ky = 0; ky < kernel_size; ++ky)
        {
            const float* input = padded.ptr<float>(y + ky) + x;
            const float* weights = kernel.data() + ky * kernel_size;
            for (int kx = 0; kx < kernel_size; ++kx)
                sum += input[kx] * weights[kx];
        }
        destination.ptr<float>(y)[x] = sum * kernel_scale;
    }
}

void convolution_sequential(const cv::Mat& padded, cv::Mat& destination)
{
    convolve_linear_range(padded, destination, 0, destination.total());
}

void convolution_opencv_parallel(const cv::Mat& padded, cv::Mat& destination)
{
    const int total = static_cast<int>(destination.total());
    cv::parallel_for_(cv::Range(0, total),
                      [&](const cv::Range& range)
                      {
                          convolve_linear_range(padded,
                                                destination,
                                                static_cast<std::size_t>(range.start),
                                                static_cast<std::size_t>(range.end));
                      });
}

void convolution_smartparallel(const cv::Mat& padded, cv::Mat& destination)
{
    const std::size_t total = destination.total();
    smart::parallel_for(0,
                        total,
                        [&](std::size_t index)
                        {
                            convolve_linear_range(padded, destination, index, index + 1);
                        });
}

std::string selected_plan()
{
    const auto& plan = smart::global_last_decision_report().plan;
    if (!plan.parallel || plan.strategy == smart::ExecutionStrategy::Sequential)
        return "Sequential";
    return std::string(smart::engine_name(plan.engine)) + "/"
           + smart::execution_strategy_name(plan.strategy) + "/w" + std::to_string(plan.job_count)
           + "/c" + std::to_string(plan.chunk_size);
}

double max_absolute_difference(const cv::Mat& a, const cv::Mat& b)
{
    CV_Assert(a.size() == b.size() && a.type() == b.type());
    double maximum = 0.0;
    cv::minMaxLoc(cv::abs(a - b), nullptr, &maximum);
    return maximum;
}

cv::Mat opencv_kernel()
{
    cv::Mat matrix(kernel_size, kernel_size, CV_32F);
    for (int y = 0; y < kernel_size; ++y)
        for (int x = 0; x < kernel_size; ++x)
            matrix.at<float>(y, x) =
                kernel[static_cast<std::size_t>(y * kernel_size + x)] * kernel_scale;
    return matrix;
}

void write_csv(const std::filesystem::path& path, const std::vector<Result>& results)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output)
        throw std::runtime_error("Could not open output CSV: " + path.string());

    output << "case,width,height,pixels,sequential_ms,opencv_parallel_ms,smartparallel_ms,"
              "opencv_filter2d_ms,smart_vs_sequential_speedup,smart_vs_opencv_parallel_speedup,"
              "smart_plan,max_manual_difference,max_opencv_difference,correct\n";
    output << std::fixed << std::setprecision(6);
    for (const Result& result : results)
    {
        output << result.case_name << ',' << result.width << ',' << result.height << ','
               << result.pixels << ',' << result.sequential_ms << ',' << result.opencv_parallel_ms
               << ',' << result.smartparallel_ms << ',' << result.opencv_filter_ms << ','
               << result.sequential_ms / result.smartparallel_ms << ','
               << result.opencv_parallel_ms / result.smartparallel_ms << ',' << result.smart_plan
               << ',' << result.max_manual_difference << ',' << result.max_opencv_difference << ','
               << (result.correct ? "true" : "false") << '\n';
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::filesystem::path output_path =
            argc > 1 ? std::filesystem::path(argv[1])
                     : std::filesystem::path("validation/output/opencv_test2_convolution.csv");

        smart::global_config().enable_experience = false;
        smart::global_config().enable_utility_model_runtime = false;
        smart::global_config().execution_engine = smart::ExecutionEngineType::Auto;

        const std::vector<Case> cases{
            {"small_128x128", 128, 128, 51},
            {"medium_640x480", 640, 480, 21},
            {"large_1920x1080", 1920, 1080, 9},
            {"xlarge_3840x2160", 3840, 2160, 5},
        };

        const cv::Mat filter_kernel = opencv_kernel();
        cv::RNG random(0xC0172026);
        std::vector<Result> results;
        bool all_correct = true;

        std::cout << "==== SmartParallel OpenCV Test 2: 5x5 convolution ====\n";
        std::cout << "OpenCV version: " << CV_VERSION << "\n\n";

        for (const Case& test_case : cases)
        {
            cv::Mat source(test_case.height, test_case.width, CV_32FC1);
            random.fill(source, cv::RNG::UNIFORM, 0.0, 255.0);
            cv::Mat padded;
            cv::copyMakeBorder(source,
                               padded,
                               kernel_radius,
                               kernel_radius,
                               kernel_radius,
                               kernel_radius,
                               cv::BORDER_REPLICATE);

            cv::Mat sequential(source.size(), source.type());
            cv::Mat opencv_parallel(source.size(), source.type());
            cv::Mat smartparallel(source.size(), source.type());
            cv::Mat opencv_reference(source.size(), source.type());

            const double sequential_ms =
                median_runtime_ms(test_case.repetitions,
                                  [&]
                                  {
                                      convolution_sequential(padded, sequential);
                                  });
            const double opencv_parallel_ms =
                median_runtime_ms(test_case.repetitions,
                                  [&]
                                  {
                                      convolution_opencv_parallel(padded, opencv_parallel);
                                  });
            const double smartparallel_ms =
                median_runtime_ms(test_case.repetitions,
                                  [&]
                                  {
                                      convolution_smartparallel(padded, smartparallel);
                                  });
            const std::string plan = selected_plan();
            const double opencv_filter_ms =
                median_runtime_ms(test_case.repetitions,
                                  [&]
                                  {
                                      cv::filter2D(source,
                                                   opencv_reference,
                                                   CV_32F,
                                                   filter_kernel,
                                                   cv::Point(-1, -1),
                                                   0.0,
                                                   cv::BORDER_REPLICATE);
                                  });

            const double manual_difference = max_absolute_difference(sequential, smartparallel);
            const double opencv_parallel_difference =
                max_absolute_difference(sequential, opencv_parallel);
            const double opencv_difference = max_absolute_difference(sequential, opencv_reference);
            const bool correct = manual_difference <= 1.0e-5 && opencv_parallel_difference <= 1.0e-5
                                 && opencv_difference <= 2.0e-4;
            all_correct = all_correct && correct;

            results.push_back(Result{test_case.name,
                                     test_case.width,
                                     test_case.height,
                                     source.total(),
                                     sequential_ms,
                                     opencv_parallel_ms,
                                     smartparallel_ms,
                                     opencv_filter_ms,
                                     plan,
                                     std::max(manual_difference, opencv_parallel_difference),
                                     opencv_difference,
                                     correct});

            std::cout << std::left << std::setw(20) << test_case.name << " pixels=" << std::setw(9)
                      << source.total() << " smart=" << std::setw(34) << plan
                      << " correct=" << (correct ? "yes" : "NO") << '\n';
            std::cout << std::right << std::fixed << std::setprecision(4)
                      << "  sequential=" << sequential_ms << " ms"
                      << " | cv::parallel_for_=" << opencv_parallel_ms << " ms"
                      << " | SmartParallel=" << smartparallel_ms << " ms"
                      << " | cv::filter2D=" << opencv_filter_ms << " ms\n"
                      << "  Smart speedup vs sequential: " << sequential_ms / smartparallel_ms
                      << "x"
                      << " | vs cv::parallel_for_: " << opencv_parallel_ms / smartparallel_ms
                      << "x\n"
                      << "  max difference: manual=" << std::scientific
                      << std::max(manual_difference, opencv_parallel_difference)
                      << " | OpenCV=" << opencv_difference << std::fixed << "\n\n";
        }

        write_csv(output_path, results);
        std::cout << "CSV written to: " << output_path.string() << '\n';
        std::cout << "Correctness: " << (all_correct ? "PASS" : "FAIL") << '\n';
        return all_correct ? 0 : 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "OpenCV Test 2 failed: " << error.what() << '\n';
        return 1;
    }
}
