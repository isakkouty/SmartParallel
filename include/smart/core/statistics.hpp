#pragma once

#include <chrono>
#include <cstddef>

namespace smart
{
    struct ExecutionStats
    {
        std::size_t iterations = 0;
        double elapsed_ms = 0.0;
    };

    class Timer
    {
    public:
        Timer()
            : start_(std::chrono::high_resolution_clock::now())
        {
        }

        double elapsed_ms() const
        {
            auto end = std::chrono::high_resolution_clock::now();

            std::chrono::duration<double, std::milli> duration = end - start_;

            return duration.count();
        }

    private:
        std::chrono::high_resolution_clock::time_point start_;
    };
}
