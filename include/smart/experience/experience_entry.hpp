#pragma once

#include <cstddef>

#include <smart/decision/execution_plan.hpp>
#include <smart/workload/fingerprint.hpp>

namespace smart
{
    struct ExperienceEntry
    {
        WorkloadFingerprint fingerprint;

        ExecutionEngineType engine = ExecutionEngineType::ThreadPool;
        ExecutionStrategy strategy = ExecutionStrategy::StaticChunks;

        double best_elapsed_ms = 0.0;
        double average_elapsed_ms = 0.0;
        double last_elapsed_ms = 0.0;

        double variance_ms = 0.0;
        double standard_deviation_ms = 0.0;

        std::size_t job_count = 1;
        std::size_t sample_count = 0;

        double confidence = 0.0;
        bool valid = false;
    };
}
