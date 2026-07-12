#pragma once

#include <string>
#include <vector>

namespace smart
{
    struct TimingPhase
    {
        std::string name;
        double elapsed_ms = 0.0;
    };

    struct TimingReport
    {
        std::vector<TimingPhase> phases;

        void clear()
        {
            phases.clear();
        }

        void add(const std::string& name, double elapsed_ms)
        {
            phases.push_back(TimingPhase{name, elapsed_ms});
        }

        double total_ms() const
        {
            double total = 0.0;

            for (const TimingPhase& phase : phases)
            {
                total += phase.elapsed_ms;
            }

            return total;
        }
    };

    inline TimingReport& global_timing_report()
    {
        static TimingReport report;
        return report;
    }

    inline const TimingReport& last_timing_report()
    {
        return global_timing_report();
    }

    inline void clear_timing_report()
    {
        global_timing_report().clear();
    }
}
