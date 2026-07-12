#include <smart/execution/parallel.hpp>
#include <smart/hardware/hardware.hpp>
#include <smart/execution/backend.hpp>
#include <smart/execution/static_thread_engine.hpp>
#include <smart/core/config.hpp>
#include <smart/core/timing_report.hpp>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

struct Vec3
{
    double x;
    double y;
    double z;
};

struct Triangle
{
    Vec3 a;
    Vec3 b;
    Vec3 c;
};

struct Quad
{
    Vec3 a;
    Vec3 b;
    Vec3 c;
    Vec3 d;
};

struct Segment
{
    Vec3 a;
    Vec3 b;
};

struct MeshElement
{
    Triangle triangle;
    Quad quad;
    bool is_quad = false;

    Vec3 barycenter{0.0, 0.0, 0.0};
    double area = 0.0;
    int intersections = 0;
};

volatile double engineering_sink = 0.0;

double now_ms()
{
    const auto now = std::chrono::high_resolution_clock::now();

    return std::chrono::duration<double, std::milli>(
        now.time_since_epoch()
    ).count();
}

Vec3 add(const Vec3& a, const Vec3& b)
{
    return Vec3{
        a.x + b.x,
        a.y + b.y,
        a.z + b.z
    };
}

Vec3 sub(const Vec3& a, const Vec3& b)
{
    return Vec3{
        a.x - b.x,
        a.y - b.y,
        a.z - b.z
    };
}

Vec3 mul(const Vec3& value, double scalar)
{
    return Vec3{
        value.x * scalar,
        value.y * scalar,
        value.z * scalar
    };
}

double dot(const Vec3& a, const Vec3& b)
{
    return
        a.x * b.x +
        a.y * b.y +
        a.z * b.z;
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return Vec3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

double length(const Vec3& value)
{
    return std::sqrt(dot(value, value));
}

Vec3 triangle_barycenter(const Triangle& triangle)
{
    return mul(
        add(add(triangle.a, triangle.b), triangle.c),
        1.0 / 3.0
    );
}

Vec3 quad_barycenter(const Quad& quad)
{
    return mul(
        add(add(quad.a, quad.b), add(quad.c, quad.d)),
        0.25
    );
}

double triangle_area(const Triangle& triangle)
{
    const Vec3 ab = sub(triangle.b, triangle.a);
    const Vec3 ac = sub(triangle.c, triangle.a);

    return 0.5 * length(cross(ab, ac));
}

Triangle quad_first_triangle(const Quad& quad)
{
    return Triangle{
        quad.a,
        quad.b,
        quad.c
    };
}

Triangle quad_second_triangle(const Quad& quad)
{
    return Triangle{
        quad.a,
        quad.c,
        quad.d
    };
}

bool segment_intersects_triangle(
    const Segment& segment,
    const Triangle& triangle)
{
    const Vec3 direction = sub(segment.b, segment.a);
    const Vec3 edge1 = sub(triangle.b, triangle.a);
    const Vec3 edge2 = sub(triangle.c, triangle.a);

    const Vec3 p_vector = cross(direction, edge2);
    const double determinant = dot(edge1, p_vector);

    if (std::abs(determinant) < 1e-9)
    {
        return false;
    }

    const double inverse_determinant = 1.0 / determinant;

    const Vec3 t_vector = sub(segment.a, triangle.a);
    const double u = dot(t_vector, p_vector) * inverse_determinant;

    if (u < 0.0 || u > 1.0)
    {
        return false;
    }

    const Vec3 q_vector = cross(t_vector, edge1);
    const double v =
        dot(direction, q_vector) * inverse_determinant;

    if (v < 0.0 || u + v > 1.0)
    {
        return false;
    }

    const double distance =
        dot(edge2, q_vector) * inverse_determinant;

    return distance >= 0.0 && distance <= 1.0;
}

void generate_mesh(std::vector<MeshElement>& elements)
{
    for (std::size_t i = 0; i < elements.size(); ++i)
    {
        const double x = static_cast<double>(i % 512);
        const double y = static_cast<double>((i / 512) % 512);
        const double z =
            static_cast<double>((i * 17) % 97) * 0.01;

        const Vec3 a{ x, y, z };
        const Vec3 b{ x + 1.0, y, z + 0.1 };
        const Vec3 c{ x, y + 1.0, z + 0.2 };
        const Vec3 d{ x + 1.0, y + 1.0, z + 0.3 };

        elements[i].triangle = Triangle{ a, b, c };
        elements[i].quad = Quad{ a, b, d, c };
        elements[i].is_quad = (i % 3) == 0;

        elements[i].barycenter = Vec3{ 0.0, 0.0, 0.0 };
        elements[i].area = 0.0;
        elements[i].intersections = 0;
    }
}

void generate_segments(std::vector<Segment>& segments)
{
    for (std::size_t i = 0; i < segments.size(); ++i)
    {
        const double x =
            static_cast<double>((i * 13) % 512);

        const double y =
            static_cast<double>((i * 29) % 512);

        segments[i].a = Vec3{
            x,
            y,
            -10.0
        };

        segments[i].b = Vec3{
            x + 0.25,
            y + 0.25,
            10.0
        };
    }
}

void engineering_work(
    MeshElement& element,
    const std::vector<Segment>& segments)
{
    if (element.is_quad)
    {
        element.barycenter =
            quad_barycenter(element.quad);
    }
    else
    {
        element.barycenter =
            triangle_barycenter(element.triangle);
    }

    int hits = 0;

    if (element.is_quad)
    {
        const Triangle first =
            quad_first_triangle(element.quad);

        const Triangle second =
            quad_second_triangle(element.quad);

        for (const Segment& segment : segments)
        {
            if (segment_intersects_triangle(segment, first))
            {
                ++hits;
            }

            if (segment_intersects_triangle(segment, second))
            {
                ++hits;
            }
        }

        element.area =
            triangle_area(first) +
            triangle_area(second);
    }
    else
    {
        for (const Segment& segment : segments)
        {
            if (segment_intersects_triangle(
                    segment,
                    element.triangle))
            {
                ++hits;
            }
        }

        element.area =
            triangle_area(element.triangle);
    }

    element.intersections = hits;
}

void consume_results(
    const std::vector<MeshElement>& elements)
{
    double sum = 0.0;

    for (const MeshElement& element : elements)
    {
        sum += element.barycenter.x;
        sum += element.barycenter.y;
        sum += element.barycenter.z;
        sum += element.area;
        sum += static_cast<double>(element.intersections);
    }

    engineering_sink += sum;
}

const char* strategy_name(
    smart::ExecutionStrategy strategy)
{
    switch (strategy)
    {
    case smart::ExecutionStrategy::Sequential:
        return "Sequential";

    case smart::ExecutionStrategy::StaticChunks:
        return "StaticChunks";

    case smart::ExecutionStrategy::DynamicChunks:
        return "DynamicChunks";
    }

    return "Unknown";
}

void raw_threaded_for(
    std::vector<MeshElement>& elements,
    const std::vector<Segment>& segments)
{
    const std::size_t total = elements.size();

    std::size_t thread_count =
        smart::hardware_threads();

    if (thread_count == 0)
    {
        thread_count = 1;
    }

    if (thread_count > total)
    {
        thread_count = total;
    }

    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (std::size_t thread_index = 0;
         thread_index < thread_count;
         ++thread_index)
    {
        const std::size_t begin =
            (total * thread_index) / thread_count;

        const std::size_t end =
            (total * (thread_index + 1)) / thread_count;

        threads.emplace_back(
            [begin, end, &elements, &segments]()
            {
                for (std::size_t i = begin;
                     i < end;
                     ++i)
                {
                    engineering_work(
                        elements[i],
                        segments
                    );
                }
            }
        );
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }
}

struct SmartTiming
{
    double total_ms = 0.0;
    double execution_ms = 0.0;
    double overhead_ms = 0.0;
};

SmartTiming read_smart_timing()
{
    SmartTiming timing;

    double generic_execution_ms = 0.0;
    double specific_execution_ms = 0.0;

    for (const smart::TimingPhase& phase :
         smart::last_timing_report().phases)
    {
        if (phase.name == "total")
        {
            timing.total_ms += phase.elapsed_ms;
        }
        else if (phase.name == "execution")
        {
            generic_execution_ms += phase.elapsed_ms;
        }
        else if (
            phase.name == "execution_static_chunks" ||
            phase.name == "execution_dynamic_chunks" ||
            phase.name == "execution_sequential")
        {
            specific_execution_ms += phase.elapsed_ms;
        }
        else
        {
            timing.overhead_ms += phase.elapsed_ms;
        }
    }

    timing.execution_ms =
        generic_execution_ms > 0.0
            ? generic_execution_ms
            : specific_execution_ms;

    return timing;
}

struct Result
{
    std::size_t element_count = 0;
    std::size_t segment_count = 0;

    double sequential_ms = 0.0;
    double raw_threaded_ms = 0.0;
    double static_thread_ms = 0.0;
    double onetbb_ms = 0.0;

    double smart_total_ms = 0.0;
    double smart_execution_ms = 0.0;
    double smart_overhead_ms = 0.0;

    const char* best_method = "Sequential";
    double best_ms = 0.0;

    smart::ExecutionPlan smart_plan;

    double difference_ms = 0.0;
    double smart_gap_percent = 0.0;
};

Result run_case(
    std::size_t element_count,
    std::size_t segment_count)
{
    constexpr int runs = 5;

    Result result;
    result.element_count = element_count;
    result.segment_count = segment_count;

    std::vector<MeshElement> elements(element_count);
    std::vector<Segment> segments(segment_count);

    generate_segments(segments);

    double sequential_total = 0.0;
    double raw_threaded_total = 0.0;
    double static_thread_total = 0.0;
    double onetbb_total = 0.0;

    double smart_total = 0.0;
    double smart_execution_total = 0.0;
    double smart_overhead_total = 0.0;

    for (int run = 0; run < runs; ++run)
    {
        double start = 0.0;

        generate_mesh(elements);

        start = now_ms();

        for (MeshElement& element : elements)
        {
            engineering_work(element, segments);
        }

        sequential_total += now_ms() - start;
        consume_results(elements);

        generate_mesh(elements);

        start = now_ms();

        raw_threaded_for(elements, segments);

        raw_threaded_total += now_ms() - start;
        consume_results(elements);

        generate_mesh(elements);

        start = now_ms();

        smart::static_thread_for_each(
            elements,
            smart::hardware_threads(),
            [&](MeshElement& element)
            {
                engineering_work(element, segments);
            }
        );

        static_thread_total += now_ms() - start;
        consume_results(elements);

        generate_mesh(elements);

        start = now_ms();

        smart::OneTbbEngine tbb_engine;

        tbb_engine.execute_range(
            elements.size(),
            smart::hardware_threads(),
            [&](std::size_t i)
            {
                engineering_work(elements[i], segments);
            }
        );

        onetbb_total += now_ms() - start;
        consume_results(elements);

        generate_mesh(elements);

        smart::for_each(
            elements,
            [&](MeshElement& element)
            {
                engineering_work(element, segments);
            }
        );

        const SmartTiming timing = read_smart_timing();

        smart_total += timing.total_ms;
        smart_execution_total += timing.execution_ms;
        smart_overhead_total += timing.overhead_ms;

        consume_results(elements);
    }

    result.sequential_ms =
        sequential_total / runs;

    result.raw_threaded_ms =
        raw_threaded_total / runs;

    result.static_thread_ms =
        static_thread_total / runs;

    result.onetbb_ms =
        onetbb_total / runs;

    result.smart_total_ms =
        smart_total / runs;

    result.smart_execution_ms =
        smart_execution_total / runs;

    result.smart_overhead_ms =
        smart_overhead_total / runs;

    result.smart_plan =
        smart::global_last_decision_report().plan;

    result.best_method = "Sequential";
    result.best_ms = result.sequential_ms;

    auto consider_best =
        [&](const char* method, double value)
        {
            if (value < result.best_ms)
            {
                result.best_method = method;
                result.best_ms = value;
            }
        };

    consider_best(
        "RawThreaded",
        result.raw_threaded_ms
    );

    consider_best(
        "StaticThread",
        result.static_thread_ms
    );

    consider_best(
        "oneTBB",
        result.onetbb_ms
    );

    consider_best(
        "SmartParallel",
        result.smart_total_ms
    );

    result.difference_ms =
        result.smart_total_ms - result.best_ms;

    result.smart_gap_percent =
        result.best_ms > 0.0
            ? (result.difference_ms / result.best_ms) * 100.0
            : 0.0;

    return result;
}

void write_csv_header(std::ofstream& csv)
{
    csv << "elements,"
        << "segments,"
        << "sequential_ms,"
        << "raw_threaded_ms,"
        << "static_thread_ms,"
        << "onetbb_ms,"
        << "smart_total_ms,"
        << "smart_execution_ms,"
        << "smart_overhead_ms,"
        << "best_method,"
        << "best_ms,"
        << "chosen_engine,"
        << "chosen_strategy,"
        << "chosen_jobs,"
        << "chosen_parallel,"
        << "difference_ms,"
        << "smart_gap_percent\n";
}

void write_csv_row(
    std::ofstream& csv,
    const Result& result)
{
    csv << result.element_count << ","
        << result.segment_count << ","
        << result.sequential_ms << ","
        << result.raw_threaded_ms << ","
        << result.static_thread_ms << ","
        << result.onetbb_ms << ","
        << result.smart_total_ms << ","
        << result.smart_execution_ms << ","
        << result.smart_overhead_ms << ","
        << result.best_method << ","
        << result.best_ms << ","
        << smart::engine_name(result.smart_plan.engine) << ","
        << strategy_name(result.smart_plan.strategy) << ","
        << result.smart_plan.job_count << ","
        << result.smart_plan.parallel << ","
        << result.difference_ms << ","
        << result.smart_gap_percent << "\n";
}

void print_result(const Result& result)
{
    std::cout << "elements=" << std::setw(8)
              << result.element_count
              << " | segments=" << std::setw(4)
              << result.segment_count
              << " | best=" << std::setw(13)
              << result.best_method
              << " (" << std::fixed << std::setprecision(6)
              << result.best_ms << " ms)"
              << " | smart="
              << smart::engine_name(result.smart_plan.engine)
              << "/"
              << strategy_name(result.smart_plan.strategy)
              << " total=" << result.smart_total_ms << " ms"
              << " exec=" << result.smart_execution_ms << " ms"
              << " overhead=" << result.smart_overhead_ms << " ms"
              << " | diff=" << result.difference_ms << " ms"
              << " | gap=" << std::setprecision(2)
              << result.smart_gap_percent
              << "%\n";
}

int main()
{
    smart::global_config().enable_experience = false;
    smart::global_config().enable_timing_diagnostics = true;

    std::ofstream csv(
        "benchmarks\\engineering_mesh\\output\\beta_1_0\\results.csv"
    );

    write_csv_header(csv);

    std::cout << "==== Engineering Mesh Benchmark ====\n";
    std::cout << "Hardware threads: "
              << smart::hardware_threads()
              << "\n\n";

    struct BenchmarkCase
    {
        std::size_t elements;
        std::size_t segments;
    };

    const std::vector<BenchmarkCase> cases =
    {
        {1'000, 16},
        {2'500, 16},
        {5'000, 32},
        {10'000, 32},
        {20'000, 64},
        {50'000, 64},
        {100'000, 64},
        {200'000, 128},
        {500'000, 128},
        {1'000'000, 256}
    };

    for (const BenchmarkCase& benchmark_case : cases)
    {
        const Result result = run_case(
            benchmark_case.elements,
            benchmark_case.segments
        );

        write_csv_row(csv, result);
        print_result(result);
    }

    std::cout << "\nResults written to:\n";
    std::cout
        << "benchmarks\\engineering_mesh\\output\\beta_1_0\\results.csv\n";

    return 0;
}