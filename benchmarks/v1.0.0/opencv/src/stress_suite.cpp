#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>
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
using Value = std::uint64_t;

struct Result
{
    std::string benchmark;
    std::size_t work_items{};
    double sequential_ms{};
    double opencv_parallel_ms{};
    double smartparallel_ms{};
    std::string smart_plan;

    bool correct{};
};

template <class Function>
double median_runtime_ms(int repetitions, Function&& function)
{
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repetitions));

    function(); // warm-up

    for (int i = 0; i < repetitions; ++i)
    {
        const auto start = Clock::now();
        function();
        const auto finish = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(finish - start).count());
    }

    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
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

template <class Function>
void opencv_for(std::size_t count, Function&& function)
{
    cv::parallel_for_(cv::Range(0, static_cast<int>(count)),
                      [&](const cv::Range& range)
                      {
                          for (int i = range.start; i < range.end; ++i)
                              function(static_cast<std::size_t>(i));
                      });
}

template <class Function>
void smart_for(std::size_t count, Function&& function)
{
    smart::parallel_for(std::size_t{0},
                        count,
                        [&](std::size_t i)
                        {
                            function(i);
                        });
}

inline Value mix(Value x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

inline std::uint32_t procedural_pixel(std::uint32_t x, std::uint32_t y, std::uint32_t frame)
{
    Value value = static_cast<Value>(x) * 0x9e3779b185ebca87ULL
                  ^ static_cast<Value>(y) * 0xc2b2ae3d27d4eb4fULL
                  ^ static_cast<Value>(frame) * 0x165667b19e3779f9ULL;

    return static_cast<std::uint32_t>(mix(value) & 0xffU);
}

template <class Task>
Result run_benchmark(const std::string& name, std::size_t work_items, int repetitions, Task&& task)
{
    std::vector<Value> sequential(work_items);
    std::vector<Value> opencv_parallel(work_items);
    std::vector<Value> smartparallel(work_items);

    const double sequential_ms = median_runtime_ms(repetitions,
                                                   [&]
                                                   {
                                                       for (std::size_t i = 0; i < work_items; ++i)
                                                           sequential[i] = task(i);
                                                   });

    const double opencv_parallel_ms = median_runtime_ms(repetitions,
                                                        [&]
                                                        {
                                                            opencv_for(work_items,
                                                                       [&](std::size_t i)
                                                                       {
                                                                           opencv_parallel[i] =
                                                                               task(i);
                                                                       });
                                                        });

    const double smartparallel_ms = median_runtime_ms(repetitions,
                                                      [&]
                                                      {
                                                          smart_for(work_items,
                                                                    [&](std::size_t i)
                                                                    {
                                                                        smartparallel[i] = task(i);
                                                                    });
                                                      });

    const std::string plan = selected_plan();
    const bool correct = sequential == opencv_parallel && sequential == smartparallel;

    return {name, work_items, sequential_ms, opencv_parallel_ms, smartparallel_ms, plan, correct};
}

Result mandelbrot_tiles_stress()
{
    constexpr std::size_t task_count = 65536;
    return run_benchmark(
        "mandelbrot_microtiles",
        task_count,
        3,
        [](std::size_t task_index) -> Value
        {
            const std::uint32_t tile_x = static_cast<std::uint32_t>(task_index & 255U);
            const std::uint32_t tile_y = static_cast<std::uint32_t>((task_index >> 8U) & 255U);

            Value checksum = 0;

            for (int py = 0; py < 8; ++py)
            {
                for (int px = 0; px < 8; ++px)
                {
                    const double x = (static_cast<double>(tile_x * 8 + px) / 2048.0) * 3.2 - 2.2;
                    const double y = (static_cast<double>(tile_y * 8 + py) / 2048.0) * 2.4 - 1.2;

                    double zr = 0.0;
                    double zi = 0.0;
                    int iteration = 0;
                    const int limit = 48 + static_cast<int>((tile_x + tile_y) & 31U);

                    while (zr * zr + zi * zi <= 4.0 && iteration < limit)
                    {
                        const double next_zr = zr * zr - zi * zi + x;
                        zi = 2.0 * zr * zi + y;
                        zr = next_zr;
                        ++iteration;
                    }

                    checksum = mix(checksum ^ static_cast<Value>(iteration + 131 * px + 977 * py));
                }
            }

            return checksum;
        });
}

Result adaptive_quadtree_stress()
{
    constexpr std::size_t task_count = 131072;

    return run_benchmark(
        "adaptive_quadtree_analysis",
        task_count,
        3,
        [](std::size_t task_index) -> Value
        {
            struct Node
            {
                std::uint32_t x;
                std::uint32_t y;
                std::uint32_t size;
                std::uint32_t depth;
            };

            Node stack[128];
            int top = 0;

            const std::uint32_t frame = static_cast<std::uint32_t>(task_index >> 12U);
            const std::uint32_t local = static_cast<std::uint32_t>(task_index & 4095U);

            stack[top++] = {(local & 63U) * 32U, ((local >> 6U) & 63U) * 32U, 32U, 0U};

            Value checksum = 0;

            while (top > 0)
            {
                const Node node = stack[--top];

                const std::uint32_t p0 = procedural_pixel(node.x, node.y, frame);
                const std::uint32_t p1 = procedural_pixel(node.x + node.size - 1U, node.y, frame);
                const std::uint32_t p2 = procedural_pixel(node.x, node.y + node.size - 1U, frame);
                const std::uint32_t p3 =
                    procedural_pixel(node.x + node.size - 1U, node.y + node.size - 1U, frame);

                const std::uint32_t minimum = std::min(std::min(p0, p1), std::min(p2, p3));

                const std::uint32_t maximum = std::max(std::max(p0, p1), std::max(p2, p3));
                const std::uint32_t contrast = maximum - minimum;

                checksum = mix(
                    checksum ^ static_cast<Value>(contrast + node.depth * 257U + node.size * 17U));

                if (node.size > 2U && node.depth < 4U && contrast > 54U)
                {
                    const std::uint32_t half = node.size / 2U;

                    stack[top++] = {node.x, node.y, half, node.depth + 1U};
                    stack[top++] = {node.x + half, node.y, half, node.depth + 1U};
                    stack[top++] = {node.x, node.y + half, half, node.depth + 1U};
                    stack[top++] = {node.x + half, node.y + half, half, node.depth + 1U};
                }
            }

            return checksum;
        });
}

Result monte_carlo_sampling_stress()
{
    constexpr std::size_t task_count = 262144;
    return run_benchmark(
        "monte_carlo_image_sampling",
        task_count,
        3,
        [](std::size_t task_index) -> Value
        {
            Value state = mix(static_cast<Value>(task_index) + 0x9e3779b97f4a7c15ULL);

            std::uint32_t inside = 0;
            Value luminance = 0;

            for (int sample = 0; sample < 96; ++sample)
            {
                state ^= state << 13;
                state ^= state >> 7;
                state ^= state << 17;

                const std::int32_t x = static_cast<std::int32_t>(state & 0xffffU) - 32768;

                const std::int32_t y = static_cast<std::int32_t>((state >> 16U) & 0xffffU) - 32768;

                const std::int64_t radius =
                    static_cast<std::int64_t>(x) * x + static_cast<std::int64_t>(y) * y;

                inside += radius < static_cast<std::int64_t>(32768) * 32768;

                luminance += procedural_pixel(static_cast<std::uint32_t>(x + 32768),
                                              static_cast<std::uint32_t>(y + 32768),
                                              static_cast<std::uint32_t>(task_index));
            }

            return mix(static_cast<Value>(inside) ^ (luminance << 16U) ^ state);
        });
}

Result sparse_morphology_stress()
{
    constexpr std::size_t task_count = 262144;

    return run_benchmark(
        "sparse_irregular_morphology",
        task_count,
        3,
        [](std::size_t task_index) -> Value
        {
            const std::uint32_t x = static_cast<std::uint32_t>(task_index & 2047U);
            const std::uint32_t y = static_cast<std::uint32_t>((task_index >> 11U) & 2047U);
            const std::uint32_t frame = static_cast<std::uint32_t>(task_index >> 22U);

            Value checksum = 0;
            int active = 0;

            for (int repeat = 0; repeat < 2 + static_cast<int>(task_index & 3U); ++repeat)
            {
                std::uint32_t minimum = 255U;
                std::uint32_t maximum = 0U;

                for (int dy = -3; dy <= 3; ++dy)
                {
                    for (int dx = -3; dx <= 3; ++dx)
                    {
                        const std::uint32_t value =
                            procedural_pixel(x + static_cast<std::uint32_t>(dx + 3),
                                             y + static_cast<std::uint32_t>(dy + 3),
                                             frame + static_cast<std::uint32_t>(repeat));

                        if (value > 224U)
                            ++active;

                        minimum = std::min(minimum, value);
                        maximum = std::max(maximum, value);

                        if (active > 18 + repeat)
                            break;
                    }

                    if (active > 18 + repeat)
                        break;
                }

                checksum =
                    mix(checksum ^ static_cast<Value>(minimum + (maximum << 8U) + active * 65537U));
            }

            return checksum;
        });
}

Result multistage_pipeline_stress()
{
    constexpr std::size_t task_count = 131072;

    return run_benchmark(
        "multistage_patch_pipeline",
        task_count,
        3,
        [](std::size_t task_index) -> Value
        {
            const std::uint32_t base_x = static_cast<std::uint32_t>((task_index & 1023U) * 8U);
            const std::uint32_t base_y =
                static_cast<std::uint32_t>(((task_index >> 10U) & 1023U) * 8U);
            const std::uint32_t frame = static_cast<std::uint32_t>(task_index >> 20U);

            std::uint32_t patch[10][10]{};
            std::int32_t blurred[8][8]{};

            for (int y = 0; y < 10; ++y)
            {
                for (int x = 0; x < 10; ++x)
                {
                    patch[y][x] = procedural_pixel(base_x + static_cast<std::uint32_t>(x),
                                                   base_y + static_cast<std::uint32_t>(y),
                                                   frame);
                }
            }

            for (int y = 1; y < 9; ++y)
            {
                for (int x = 1; x < 9; ++x)
                {
                    blurred[y - 1][x - 1] =
                        static_cast<std::int32_t>(
                            patch[y - 1][x - 1] + 2U * patch[y - 1][x] + patch[y - 1][x + 1]
                            + 2U * patch[y][x - 1] + 4U * patch[y][x] + 2U * patch[y][x + 1]
                            + patch[y + 1][x - 1] + 2U * patch[y + 1][x] + patch[y + 1][x + 1])
                        / 16;
                }
            }

            Value checksum = 0;

            for (int y = 1; y < 7; ++y)
            {
                for (int x = 1; x < 7; ++x)
                {
                    const int gx = -blurred[y - 1][x - 1] + blurred[y - 1][x + 1]
                                   - 2 * blurred[y][x - 1] + 2 * blurred[y][x + 1]
                                   - blurred[y + 1][x - 1] + blurred[y + 1][x + 1];

                    const int gy = -blurred[y - 1][x - 1] - 2 * blurred[y - 1][x]
                                   - blurred[y - 1][x + 1] + blurred[y + 1][x - 1]
                                   + 2 * blurred[y + 1][x] + blurred[y + 1][x + 1];

                    const std::uint32_t magnitude =
                        static_cast<std::uint32_t>(std::abs(gx) + std::abs(gy));

                    const std::uint32_t edge = magnitude > 320U ? 255U : 0U;

                    checksum = mix(checksum
                                   ^ static_cast<Value>(edge + magnitude * 257U
                                                        + static_cast<std::uint32_t>(x + 17 * y)));
                }
            }

            return checksum;
        });
}

Result irregular_mixed_kernel_stress()
{
    constexpr std::size_t task_count = 196608;

    return run_benchmark(
        "irregular_mixed_kernels",
        task_count,
        3,
        [](std::size_t task_index) -> Value
        {
            Value checksum = mix(static_cast<Value>(task_index));

            const int mode = static_cast<int>(task_index & 7U);
            const int repeats = 8 + static_cast<int>((task_index >> 3U) & 31U);

            for (int repeat = 0; repeat < repeats; ++repeat)
            {
                const std::uint32_t x = static_cast<std::uint32_t>(task_index + repeat * 17U);
                const std::uint32_t y = static_cast<std::uint32_t>(task_index * 3U + repeat * 29U);

                switch (mode)
                {
                    case 0:
                    case 1:
                    {
                        std::uint32_t sum = 0;
                        for (int k = 0; k < 25; ++k)
                            sum += procedural_pixel(x + static_cast<std::uint32_t>(k),
                                                    y + static_cast<std::uint32_t>(k * 3),
                                                    static_cast<std::uint32_t>(repeat));
                        checksum = mix(checksum ^ sum);
                        break;
                    }

                    case 2:
                    case 3:
                    {
                        std::uint32_t minimum = 255U;
                        std::uint32_t maximum = 0U;
                        for (int k = 0; k < 49; ++k)
                        {
                            const std::uint32_t value =
                                procedural_pixel(x + static_cast<std::uint32_t>(k % 7),
                                                 y + static_cast<std::uint32_t>(k / 7),
                                                 static_cast<std::uint32_t>(repeat));
                            minimum = std::min(minimum, value);
                            maximum = std::max(maximum, value);
                        }
                        checksum = mix(checksum ^ minimum ^ (static_cast<Value>(maximum) << 32U));
                        break;
                    }

                    case 4:
                    case 5:
                    {
                        Value state = checksum;
                        for (int k = 0; k < 32; ++k)
                            state = mix(state
                                        + procedural_pixel(x + static_cast<std::uint32_t>(k),
                                                           y,
                                                           static_cast<std::uint32_t>(repeat)));
                        checksum ^= state;
                        break;
                    }

                    default:
                    {
                        std::uint32_t transitions = 0;
                        std::uint32_t previous =
                            procedural_pixel(x, y, static_cast<std::uint32_t>(repeat)) > 127U;

                        for (int k = 1; k < 64; ++k)
                        {
                            const std::uint32_t current =
                                procedural_pixel(x + static_cast<std::uint32_t>(k),
                                                 y + static_cast<std::uint32_t>(k / 8),
                                                 static_cast<std::uint32_t>(repeat))
                                > 127U;

                            transitions += current != previous;
                            previous = current;
                        }

                        checksum = mix(checksum ^ transitions);
                        break;
                    }
                }
            }

            return checksum;
        });
}

void write_csv(const std::filesystem::path& path, const std::vector<Result>& results)
{
    std::filesystem::create_directories(path.parent_path());

    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("Could not open output CSV: " + path.string());

    out << "benchmark,work_items,sequential_ms,"
           "opencv_parallel_ms,smartparallel_ms,"
           "smart_vs_sequential_speedup,"
           "smart_vs_opencv_parallel_speedup,"
           "smart_plan,correct\n";

    out << std::fixed << std::setprecision(6);

    for (const auto& result : results)
    {
        out << result.benchmark << ',' << result.work_items << ',' << result.sequential_ms << ','
            << result.opencv_parallel_ms << ',' << result.smartparallel_ms << ','
            << result.sequential_ms / result.smartparallel_ms << ','
            << result.opencv_parallel_ms / result.smartparallel_ms << ',' << result.smart_plan
            << ',' << (result.correct ? "true" : "false") << '\n';
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::filesystem::path output =
            argc > 1 ? argv[1] : "validation/output/opencv_stress_suite.csv";

        smart::global_config().enable_experience = false;
        smart::global_config().enable_utility_model_runtime = false;
        smart::global_config().execution_engine = smart::ExecutionEngineType::Auto;

        std::vector<Result> results;
        results.reserve(6);

        results.push_back(mandelbrot_tiles_stress());
        results.push_back(adaptive_quadtree_stress());
        results.push_back(monte_carlo_sampling_stress());
        results.push_back(sparse_morphology_stress());
        results.push_back(multistage_pipeline_stress());
        results.push_back(irregular_mixed_kernel_stress());

        bool all_correct = true;

        std::cout << "==== SmartParallel OpenCV scheduler stress suite ====\n"
                     "OpenCV version: "
                  << CV_VERSION << "\n\n";

        for (const auto& result : results)
        {
            all_correct = all_correct && result.correct;

            std::cout << std::left << std::setw(32) << result.benchmark << " work=" << std::setw(9)
                      << result.work_items << " smart=" << std::setw(36) << result.smart_plan
                      << " correct=" << (result.correct ? "yes" : "NO") << '\n'
                      << std::right << std::fixed << std::setprecision(4)
                      << "  sequential=" << result.sequential_ms
                      << " ms | cv::parallel_for_=" << result.opencv_parallel_ms
                      << " ms | SmartParallel=" << result.smartparallel_ms << " ms\n"
                      << "  speedup vs sequential="
                      << result.sequential_ms / result.smartparallel_ms
                      << "x | vs cv::parallel_for_="
                      << result.opencv_parallel_ms / result.smartparallel_ms << "x\n\n";
        }

        write_csv(output, results);

        std::cout << "CSV written to: " << output.string()
                  << "\nCorrectness: " << (all_correct ? "PASS" : "FAIL") << '\n';

        return all_correct ? 0 : 2;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "OpenCV stress suite failed: " << exception.what() << '\n';

        return 1;
    }
}
