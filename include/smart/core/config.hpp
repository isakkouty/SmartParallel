#pragma once

namespace smart
{
    enum class ExecutionEngineType
    {
        Auto,
        ThreadPool,
        StaticThread,
        OneTbb
    };

    struct Config
    {
        bool enable_timing_diagnostics = false;
        bool enable_experience = true;

        ExecutionEngineType execution_engine = ExecutionEngineType::Auto;
    };

    inline Config& global_config()
    {
        static Config config;
        return config;
    }
}
