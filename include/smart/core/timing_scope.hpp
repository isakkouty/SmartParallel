#pragma once

#include <smart/core/config.hpp>
#include <smart/core/statistics.hpp>
#include <smart/core/timing_report.hpp>
#include <string>

namespace smart
{
class TimingScope
{
  public:
    explicit TimingScope(const std::string& name)
        : name_(name)
    {
    }

    ~TimingScope()
    {
        if (global_config().enable_timing_diagnostics)
        {
            global_timing_report().add(name_, timer_.elapsed_ms());
        }
    }

  private:
    std::string name_;
    Timer timer_;
};
} // namespace smart
