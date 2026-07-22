#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <smart/core/config.hpp>
#include <smart/decision/decision_report.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/execution/parallel.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

struct Row
{
    std::string domain, benchmark, case_name;
    std::size_t work_items = 0, width = 0, height = 0, steps = 0;
    std::uint64_t auxiliary_work = 0;
    int repetitions = 0;
    double sequential_ms = 0, forced_onetbb_ms = 0, adaptive_ms = 0, best_ms = 0;
    double forced_onetbb_speedup = 0, adaptive_speedup = 0, adaptive_regret = 0;

    std::string best_backend, adaptive_backend, adaptive_strategy;
    std::size_t workers = 1, chunk_size = 0;

    bool decision_correct = false, output_correct = false, cache_hit = false,
         sequential_fast_path = false;
    double max_error = 0, sequential_checksum = 0, forced_checksum = 0, adaptive_checksum = 0;
    bool profile_available = false;

    std::size_t sampled_iterations = 0;

    double estimated_sequential_ms = 0, estimated_parallel_ms = 0, predicted_speedup = 0;

    double cache_lookup_ms = 0, workload_analysis_ms = 0, profiling_ms = 0, decision_ms = 0,
           execution_ms = 0, total_ms = 0;
};

Row make_row(std::string domain,
             std::string benchmark,
             std::string case_name,
             std::size_t work_items,
             std::size_t width,
             std::size_t height,
             std::size_t steps,
             std::uint64_t auxiliary_work,
             int repetitions)
{
    Row row{};
    row.domain = std::move(domain);
    row.benchmark = std::move(benchmark);
    row.case_name = std::move(case_name);
    row.work_items = work_items;
    row.width = width;
    row.height = height;
    row.steps = steps;
    row.auxiliary_work = auxiliary_work;
    row.repetitions = repetitions;
    return row;
}

template <class F>
double median_ms(int repetitions, F&& f)
{
    std::vector<double> samples;
    samples.reserve(repetitions);
    f();
    for (int r = 0; r < repetitions; ++r)
    {
        auto a = Clock::now();
        f();
        auto b = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(b - a).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

template <class F>
void forced_tbb(std::size_t n, F&& f)
{
    oneapi::tbb::parallel_for(
        oneapi::tbb::blocked_range<std::size_t>(0, n),
        [&](const oneapi::tbb::blocked_range<std::size_t>& r)
        {
            for (std::size_t i = r.begin(); i < r.end(); ++i)
                f(i);
        },
        oneapi::tbb::auto_partitioner{});
}

template <class T>
double max_error(const std::vector<T>& a, const std::vector<T>& b)
{
    double e = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        e = std::max(e, std::abs(double(a[i]) - double(b[i])));
    return e;
}
template <class T>
double checksum(const std::vector<T>& a)
{
    long double s = 0;
    for (const auto& v : a)
        s += static_cast<long double>(v);
    return static_cast<double>(s);
}

const char* strategy_name(smart::ExecutionStrategy s)
{
    switch (s)
    {
        case smart::ExecutionStrategy::Sequential:
            return "Sequential";
        case smart::ExecutionStrategy::StaticChunks:
            return "StaticChunks";
        case smart::ExecutionStrategy::DynamicChunks:
            return "DynamicChunks";
        default:
            return "Unknown";
    }
}

void finish(Row& r)
{
    r.best_ms = std::min(r.sequential_ms, r.forced_onetbb_ms);
    r.best_backend = (r.sequential_ms <= r.forced_onetbb_ms) ? "Sequential" : "oneTBB";
    r.forced_onetbb_speedup = r.sequential_ms / r.forced_onetbb_ms;
    r.adaptive_speedup = r.sequential_ms / r.adaptive_ms;
    r.adaptive_regret = r.adaptive_ms / r.best_ms;
    const auto& rep = smart::global_last_decision_report();
    const auto& d = smart::global_last_parallel_for_profile_diagnostics();
    r.adaptive_backend = rep.plan.parallel ? smart::engine_name(rep.plan.engine) : "Sequential";
    r.adaptive_strategy = strategy_name(rep.plan.strategy);
    r.workers = rep.plan.job_count;
    r.chunk_size = rep.plan.chunk_size;
    r.cache_hit = d.cache_hit;
    r.sequential_fast_path = d.sequential_fast_path;
    r.profile_available = d.profile_available;
    r.sampled_iterations = d.sampled_iterations;
    r.estimated_sequential_ms = d.estimated_sequential_ms;
    r.estimated_parallel_ms = d.estimated_parallel_ms;
    r.predicted_speedup = d.predicted_speedup;
    r.cache_lookup_ms = d.cache_lookup_ms;
    r.workload_analysis_ms = d.workload_analysis_ms;
    r.profiling_ms = d.profiling_ms;
    r.decision_ms = d.decision_ms;
    r.execution_ms = d.execution_ms;
    r.total_ms = d.total_ms;
    r.decision_correct = (r.adaptive_backend == r.best_backend) || (r.adaptive_regret <= 1.10);
}

std::vector<unsigned char> make_bytes(std::size_t n)
{
    std::vector<unsigned char> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = static_cast<unsigned char>((i * 37u + i / 7u) % 256u);
    return v;
}

Row threshold(std::string name, std::size_t w, std::size_t h, int reps)
{
    Row r = make_row("computer_vision", "threshold", std::move(name), w * h, w, h, 0, 0, reps);
    auto in = make_bytes(r.work_items);
    std::vector<unsigned char> s(r.work_items), t(r.work_items), a(r.work_items);
    auto op = [&](auto& out, auto runner)
    {
        runner(r.work_items,
               [&](std::size_t i)
               {
                   out[i] = in[i] > 127 ? 255 : 0;
               });
    };
    r.sequential_ms = median_ms(reps,
                                [&]
                                {
                                    op(s,
                                       [](std::size_t n, auto f)
                                       {
                                           for (std::size_t i = 0; i < n; ++i)
                                               f(i);
                                       });
                                });
    r.forced_onetbb_ms = median_ms(reps,
                                   [&]
                                   {
                                       op(t,
                                          [](std::size_t n, auto f)
                                          {
                                              forced_tbb(n, f);
                                          });
                                   });
    r.adaptive_ms = median_ms(reps,
                              [&]
                              {
                                  op(a,
                                     [](std::size_t n, auto f)
                                     {
                                         smart::parallel_for(0, n, f);
                                     });
                              });
    op(s,
       [](std::size_t n, auto f)
       {
           for (std::size_t i = 0; i < n; ++i)
               f(i);
       });
    op(t,
       [](std::size_t n, auto f)
       {
           forced_tbb(n, f);
       });
    op(a,
       [](std::size_t n, auto f)
       {
           smart::parallel_for(0, n, f);
       });
    r.max_error = std::max(max_error(s, t), max_error(s, a));
    r.output_correct = r.max_error == 0;
    r.sequential_checksum = checksum(s);
    r.forced_checksum = checksum(t);
    r.adaptive_checksum = checksum(a);
    finish(r);
    return r;
}

std::vector<float> make_image(std::size_t w, std::size_t h)
{
    std::vector<float> v(w * h);
    for (std::size_t y = 0; y < h; ++y)
        for (std::size_t x = 0; x < w; ++x)
            v[y * w + x] = float((x * 13 + y * 17 + (x * y) % 31) % 256);
    return v;
}

Row convolution(std::string name, std::size_t w, std::size_t h, int reps)
{
    Row r =
        make_row("computer_vision", "convolution_5x5", std::move(name), w * h, w, h, 0, 0, reps);
    auto in = make_image(w, h);
    std::vector<float> s(w * h), t(w * h), a(w * h);
    const float k[5][5] = {
        {1, 2, 3, 2, 1}, {2, 4, 6, 4, 2}, {3, 6, 9, 6, 3}, {2, 4, 6, 4, 2}, {1, 2, 3, 2, 1}};
    auto calc = [&](std::size_t idx)
    {
        std::size_t y = idx / w, x = idx % w;
        if (x < 2 || y < 2 || x + 2 >= w || y + 2 >= h)
            return in[idx];
        float z = 0;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                z += in[(y + dy) * w + (x + dx)] * k[dy + 2][dx + 2];
        return z / 81.0f;
    };
    auto op = [&](auto& out, auto runner)
    {
        runner(w * h,
               [&](std::size_t i)
               {
                   out[i] = calc(i);
               });
    };
    r.sequential_ms = median_ms(reps,
                                [&]
                                {
                                    op(s,
                                       [](auto n, auto f)
                                       {
                                           for (std::size_t i = 0; i < n; ++i)
                                               f(i);
                                       });
                                });
    r.forced_onetbb_ms = median_ms(reps,
                                   [&]
                                   {
                                       op(t,
                                          [](auto n, auto f)
                                          {
                                              forced_tbb(n, f);
                                          });
                                   });
    r.adaptive_ms = median_ms(reps,
                              [&]
                              {
                                  op(a,
                                     [](auto n, auto f)
                                     {
                                         smart::parallel_for(0, n, f);
                                     });
                              });
    op(s,
       [](auto n, auto f)
       {
           for (std::size_t i = 0; i < n; ++i)
               f(i);
       });
    op(t,
       [](auto n, auto f)
       {
           forced_tbb(n, f);
       });
    op(a,
       [](auto n, auto f)
       {
           smart::parallel_for(0, n, f);
       });
    r.max_error = std::max(max_error(s, t), max_error(s, a));
    r.output_correct = r.max_error <= 1e-5;
    r.sequential_checksum = checksum(s);
    r.forced_checksum = checksum(t);
    r.adaptive_checksum = checksum(a);
    finish(r);
    return r;
}

Row sobel(std::string name, std::size_t w, std::size_t h, int reps)
{
    Row r = make_row("computer_vision", "sobel", std::move(name), w * h, w, h, 0, 0, reps);
    auto in = make_image(w, h);
    std::vector<float> s(w * h), t(w * h), a(w * h);
    auto calc = [&](std::size_t idx)
    {
        std::size_t y = idx / w, x = idx % w;
        if (x == 0 || y == 0 || x + 1 >= w || y + 1 >= h)
            return 0.0f;
        float gx = -in[(y - 1) * w + x - 1] + in[(y - 1) * w + x + 1] - 2 * in[y * w + x - 1]
                   + 2 * in[y * w + x + 1] - in[(y + 1) * w + x - 1] + in[(y + 1) * w + x + 1];
        float gy = -in[(y - 1) * w + x - 1] - 2 * in[(y - 1) * w + x] - in[(y - 1) * w + x + 1]
                   + in[(y + 1) * w + x - 1] + 2 * in[(y + 1) * w + x] + in[(y + 1) * w + x + 1];
        return std::sqrt(gx * gx + gy * gy);
    };
    auto op = [&](auto& out, auto runner)
    {
        runner(w * h,
               [&](std::size_t i)
               {
                   out[i] = calc(i);
               });
    };
    r.sequential_ms = median_ms(reps,
                                [&]
                                {
                                    op(s,
                                       [](auto n, auto f)
                                       {
                                           for (std::size_t i = 0; i < n; ++i)
                                               f(i);
                                       });
                                });
    r.forced_onetbb_ms = median_ms(reps,
                                   [&]
                                   {
                                       op(t,
                                          [](auto n, auto f)
                                          {
                                              forced_tbb(n, f);
                                          });
                                   });
    r.adaptive_ms = median_ms(reps,
                              [&]
                              {
                                  op(a,
                                     [](auto n, auto f)
                                     {
                                         smart::parallel_for(0, n, f);
                                     });
                              });
    op(s,
       [](auto n, auto f)
       {
           for (std::size_t i = 0; i < n; ++i)
               f(i);
       });
    op(t,
       [](auto n, auto f)
       {
           forced_tbb(n, f);
       });
    op(a,
       [](auto n, auto f)
       {
           smart::parallel_for(0, n, f);
       });
    r.max_error = std::max(max_error(s, t), max_error(s, a));
    r.output_correct = r.max_error <= 1e-5;
    r.sequential_checksum = checksum(s);
    r.forced_checksum = checksum(t);
    r.adaptive_checksum = checksum(a);
    finish(r);
    return r;
}

Row integration(std::string name, std::size_t n, int reps)
{
    Row r = make_row("scientific", "numerical_integration", std::move(name), n, 0, 0, 0, 0, reps);
    std::vector<double> s(n), t(n), a(n);
    double dx = 100.0 / double(n);
    auto calc = [&](std::size_t i)
    {
        double x = (double(i) + 0.5) * dx;
        return std::sin(x) * std::exp(-0.1 * x) * dx;
    };
    auto op = [&](auto& out, auto runner)
    {
        runner(n,
               [&](std::size_t i)
               {
                   out[i] = calc(i);
               });
    };
    r.sequential_ms = median_ms(reps,
                                [&]
                                {
                                    op(s,
                                       [](auto m, auto f)
                                       {
                                           for (std::size_t i = 0; i < m; ++i)
                                               f(i);
                                       });
                                });
    r.forced_onetbb_ms = median_ms(reps,
                                   [&]
                                   {
                                       op(t,
                                          [](auto m, auto f)
                                          {
                                              forced_tbb(m, f);
                                          });
                                   });
    r.adaptive_ms = median_ms(reps,
                              [&]
                              {
                                  op(a,
                                     [](auto m, auto f)
                                     {
                                         smart::parallel_for(0, m, f);
                                     });
                              });
    op(s,
       [](auto m, auto f)
       {
           for (std::size_t i = 0; i < m; ++i)
               f(i);
       });
    op(t,
       [](auto m, auto f)
       {
           forced_tbb(m, f);
       });
    op(a,
       [](auto m, auto f)
       {
           smart::parallel_for(0, m, f);
       });
    r.max_error = std::max(max_error(s, t), max_error(s, a));
    r.output_correct = r.max_error <= 1e-12;
    r.sequential_checksum = checksum(s);
    r.forced_checksum = checksum(t);
    r.adaptive_checksum = checksum(a);
    finish(r);
    return r;
}

Row heat(std::string name, std::size_t w, std::size_t h, std::size_t steps, int reps)
{
    Row r = make_row("scientific", "heat_diffusion", std::move(name), w * h, w, h, steps, 0, reps);
    auto init = make_image(w, h);
    for (std::size_t x = 0; x < w; ++x)
    {
        init[x] = 0;
        init[(h - 1) * w + x] = 0;
    }
    for (std::size_t y = 0; y < h; ++y)
    {
        init[y * w] = 0;
        init[y * w + w - 1] = 0;
    }
    std::vector<float> s, t, a;
    auto run = [&](std::vector<float>& out, auto runner)
    {
        std::vector<float> cur = init, next = init;
        for (std::size_t st = 0; st < steps; ++st)
        {
            runner(h - 2,
                   [&](std::size_t row)
                   {
                       std::size_t y = row + 1;
                       for (std::size_t x = 1; x + 1 < w; ++x)
                       {
                           auto i = y * w + x;
                           next[i] = cur[i]
                                     + 0.2f
                                           * (cur[i - 1] + cur[i + 1] + cur[i - w] + cur[i + w]
                                              - 4 * cur[i]);
                       }
                   });
            std::swap(cur, next);
        }
        out = std::move(cur);
    };
    r.sequential_ms = median_ms(reps,
                                [&]
                                {
                                    run(s,
                                        [](auto n, auto f)
                                        {
                                            for (std::size_t i = 0; i < n; ++i)
                                                f(i);
                                        });
                                });
    r.forced_onetbb_ms = median_ms(reps,
                                   [&]
                                   {
                                       run(t,
                                           [](auto n, auto f)
                                           {
                                               forced_tbb(n, f);
                                           });
                                   });
    r.adaptive_ms = median_ms(reps,
                              [&]
                              {
                                  run(a,
                                      [](auto n, auto f)
                                      {
                                          smart::parallel_for(0, n, f);
                                      });
                              });
    run(s,
        [](auto n, auto f)
        {
            for (std::size_t i = 0; i < n; ++i)
                f(i);
        });
    run(t,
        [](auto n, auto f)
        {
            forced_tbb(n, f);
        });
    run(a,
        [](auto n, auto f)
        {
            smart::parallel_for(0, n, f);
        });
    r.max_error = std::max(max_error(s, t), max_error(s, a));
    r.output_correct = r.max_error <= 1e-5;
    r.sequential_checksum = checksum(s);
    r.forced_checksum = checksum(t);
    r.adaptive_checksum = checksum(a);
    finish(r);
    return r;
}

std::uint32_t mix32(std::uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return v;
}
struct Particle
{
    double x, y, z, m;
    std::uint32_t work;
};
Row particles(std::string name, std::size_t n, int reps)
{
    Row r = make_row("scientific", "irregular_particles", std::move(name), n, 0, 0, 0, 0, reps);

    std::vector<Particle> particles_data(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::uint32_t hash = mix32(static_cast<std::uint32_t>(i + 1));
        const double q = static_cast<double>(i) * 0.000173;

        Particle particle{};
        particle.x = std::sin(q * 1.7) * 50.0 + static_cast<double>(hash & 255U) * 0.001;
        particle.y = std::cos(q * 1.3) * 40.0 + static_cast<double>((hash >> 8U) & 255U) * 0.001;
        particle.z = std::sin(q * 0.7) * std::cos(q * 1.1) * 30.0;
        particle.m = 0.5 + static_cast<double>((hash >> 16U) & 1023U) / 1024.0;
        particle.work = 16U + hash % 496U;
        particles_data[i] = particle;
        r.auxiliary_work += particle.work;
    }

    std::vector<double> sequential(n);
    std::vector<double> forced(n);
    std::vector<double> adaptive(n);

    const auto calculate = [&](std::size_t i)
    {
        const Particle& particle = particles_data[i];
        double energy = 0.0;
        const double seed = 0.000031 * static_cast<double>(i + 1);

        for (std::uint32_t k = 0; k < particle.work; ++k)
        {
            const double phase = seed + 0.013 * static_cast<double>(k + 1);
            const double dx = particle.x + std::sin(phase) * 3.0;
            const double dy = particle.y + std::cos(phase * 1.37) * 2.0;
            const double dz = particle.z + std::sin(phase * 0.73) * 1.5;
            const double radius_squared = dx * dx + dy * dy + dz * dz + 1.0;
            energy += particle.m * (std::sin(radius_squared * 0.001) + std::cos(phase))
                      / std::sqrt(radius_squared);
        }
        return energy;
    };

    const auto run = [&](std::vector<double>& output, const auto& runner)
    {
        runner(n,
               [&](std::size_t i)
               {
                   output[i] = calculate(i);
               });
    };

    const auto sequential_runner = [](std::size_t count, const auto& function)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            function(i);
        }
    };
    const auto tbb_runner = [](std::size_t count, const auto& function)
    {
        forced_tbb(count, function);
    };
    const auto adaptive_runner = [](std::size_t count, const auto& function)
    {
        smart::parallel_for(0, count, function);
    };

    r.sequential_ms = median_ms(reps,
                                [&]
                                {
                                    run(sequential, sequential_runner);
                                });
    r.forced_onetbb_ms = median_ms(reps,
                                   [&]
                                   {
                                       run(forced, tbb_runner);
                                   });
    r.adaptive_ms = median_ms(reps,
                              [&]
                              {
                                  run(adaptive, adaptive_runner);
                              });

    run(sequential, sequential_runner);
    run(forced, tbb_runner);
    run(adaptive, adaptive_runner);

    r.max_error = std::max(max_error(sequential, forced), max_error(sequential, adaptive));
    r.output_correct = r.max_error <= 1e-12;
    r.sequential_checksum = checksum(sequential);
    r.forced_checksum = checksum(forced);
    r.adaptive_checksum = checksum(adaptive);
    finish(r);
    return r;
}

void write_csv(const std::filesystem::path& path, const std::vector<Row>& rows)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output)
    {
        throw std::runtime_error("Unable to open decision-quality CSV: " + path.string());
    }

    output << "schema_version,domain,benchmark,case,work_items,width,height,steps,"
           << "auxiliary_work,repetitions,hardware_threads,sequential_ms,forced_onetbb_ms,"
           << "adaptive_ms,best_ms,forced_onetbb_speedup,adaptive_speedup,adaptive_regret,"
           << "best_backend,adaptive_backend,adaptive_strategy,workers,chunk_size,"
           << "decision_correct,output_correct,max_error,sequential_checksum,"
           << "forced_onetbb_checksum,adaptive_checksum,cache_hit,sequential_fast_path,"
           << "profile_available,sampled_iterations,estimated_sequential_ms,"
           << "estimated_parallel_ms,predicted_speedup,cache_lookup_ms,workload_analysis_ms,"
           << "profiling_ms,decision_ms,execution_ms,total_ms\n";

    output << std::fixed << std::setprecision(12);
    for (const Row& r : rows)
    {
        output << "2," << r.domain << ',' << r.benchmark << ',' << r.case_name << ','
               << r.work_items << ',' << r.width << ',' << r.height << ',' << r.steps << ','
               << r.auxiliary_work << ',' << r.repetitions << ','
               << std::thread::hardware_concurrency() << ',' << r.sequential_ms << ','
               << r.forced_onetbb_ms << ',' << r.adaptive_ms << ',' << r.best_ms << ','
               << r.forced_onetbb_speedup << ',' << r.adaptive_speedup << ',' << r.adaptive_regret
               << ',' << r.best_backend << ',' << r.adaptive_backend << ',' << r.adaptive_strategy
               << ',' << r.workers << ',' << r.chunk_size << ','
               << (r.decision_correct ? "true" : "false") << ','
               << (r.output_correct ? "true" : "false") << ',' << r.max_error << ','
               << r.sequential_checksum << ',' << r.forced_checksum << ',' << r.adaptive_checksum
               << ',' << (r.cache_hit ? "true" : "false") << ','
               << (r.sequential_fast_path ? "true" : "false") << ','
               << (r.profile_available ? "true" : "false") << ',' << r.sampled_iterations << ','
               << r.estimated_sequential_ms << ',' << r.estimated_parallel_ms << ','
               << r.predicted_speedup << ',' << r.cache_lookup_ms << ',' << r.workload_analysis_ms
               << ',' << r.profiling_ms << ',' << r.decision_ms << ',' << r.execution_ms << ','
               << r.total_ms << '\n';
    }
}

} // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path output_path =
        argc > 1 ? std::filesystem::path(argv[1])
                 : std::filesystem::path("validation/output/all_benchmarks_decision_quality.csv");

    auto& config = smart::global_config();
    config.enable_experience = false;
    config.enable_utility_model_runtime = false;
    config.execution_engine = smart::ExecutionEngineType::Auto;
    config.enable_timing_diagnostics = true;

    std::vector<Row> rows;
    const auto add = [&](Row row)
    {
        std::cout << row.domain << '/' << row.benchmark << '/' << row.case_name
                  << " best=" << row.best_backend << " adaptive=" << row.adaptive_backend
                  << " regret=" << std::fixed << std::setprecision(3) << row.adaptive_regret
                  << " decision=" << (row.decision_correct ? "PASS" : "MISS")
                  << " correctness=" << (row.output_correct ? "PASS" : "FAIL") << '\n';
        rows.push_back(std::move(row));
    };

    using ImageCase = std::tuple<std::string, std::size_t, std::size_t, int>;
    const std::vector<ImageCase> threshold_cases{
        {"tiny", 320, 240, 9},
        {"small", 640, 480, 9},
        {"medium", 1920, 1080, 7},
        {"large", 3840, 2160, 5},
    };
    for (const auto& test_case : threshold_cases)
    {
        add(threshold(std::get<0>(test_case),
                      std::get<1>(test_case),
                      std::get<2>(test_case),
                      std::get<3>(test_case)));
    }

    const std::vector<ImageCase> convolution_cases{
        {"tiny", 128, 128, 9},
        {"small", 512, 512, 7},
        {"medium", 1024, 1024, 5},
        {"large", 2048, 2048, 5},
    };
    for (const auto& test_case : convolution_cases)
    {
        add(convolution(std::get<0>(test_case),
                        std::get<1>(test_case),
                        std::get<2>(test_case),
                        std::get<3>(test_case)));
        add(sobel(std::get<0>(test_case),
                  std::get<1>(test_case),
                  std::get<2>(test_case),
                  std::get<3>(test_case)));
    }

    using CountCase = std::tuple<std::string, std::size_t, int>;
    const std::vector<CountCase> integration_cases{
        {"tiny", 10000, 11},
        {"small", 100000, 9},
        {"medium", 1000000, 7},
        {"large", 10000000, 5},
    };
    for (const auto& test_case : integration_cases)
    {
        add(integration(std::get<0>(test_case), std::get<1>(test_case), std::get<2>(test_case)));
    }

    using HeatCase = std::tuple<std::string, std::size_t, std::size_t, std::size_t, int>;
    const std::vector<HeatCase> heat_cases{
        {"tiny", 128, 128, 30, 7},
        {"small", 512, 512, 40, 5},
        {"medium", 1024, 1024, 40, 5},
        {"large", 2048, 2048, 30, 3},
    };
    for (const auto& test_case : heat_cases)
    {
        add(heat(std::get<0>(test_case),
                 std::get<1>(test_case),
                 std::get<2>(test_case),
                 std::get<3>(test_case),
                 std::get<4>(test_case)));
    }

    const std::vector<CountCase> particle_cases{
        {"tiny", 1000, 7},
        {"small", 10000, 5},
        {"medium", 100000, 3},
        {"large", 500000, 3},
    };
    for (const auto& test_case : particle_cases)
    {
        add(particles(std::get<0>(test_case), std::get<1>(test_case), std::get<2>(test_case)));
    }

    write_csv(output_path, rows);
    const bool correct = std::all_of(rows.begin(),
                                     rows.end(),
                                     [](const Row& row)
                                     {
                                         return row.output_correct;
                                     });

    std::cout << "CSV written to: " << output_path.string() << '\n'
              << "Correctness: " << (correct ? "PASS" : "FAIL") << '\n';
    return correct ? 0 : 1;
}
