#pragma once

#include <iostream>

#include <smart/decision/decision_report.hpp>
#include <smart/decision/execution_plan.hpp>

namespace smart
{
    inline void print_decision_report(
        const DecisionReport& report,
        std::ostream& out = std::cout)
    {
        out << "==== SmartParallel Decision Report ====\n";
        out << "Iterations: "
            << report.model.workload.iterations << "\n";

        out << "Working set bytes: "
            << report.model.workload.working_set_bytes << "\n";

        out << "L1 pressure: "
            << report.model.l1_pressure << "\n";

        out << "L2 pressure: "
            << report.model.l2_pressure << "\n";

        out << "L3 pressure: "
            << report.model.l3_pressure << "\n";

        out << "Page pressure: "
            << report.model.page_pressure << "\n";

        out << "Likely memory sensitive: "
            << report.model.likely_memory_sensitive << "\n";

        out << "Scheduling preference: ";

        switch (report.execution.scheduling)
        {
        case SchedulingPreference::Sequential:
            out << "Sequential";
            break;

        case SchedulingPreference::Static:
            out << "Static";
            break;

        case SchedulingPreference::Dynamic:
            out << "Dynamic";
            break;

        default:
            out << "Unknown";
            break;
        }

        out << "\n";

        out << "Memory locality critical: "
            << report.execution.memory_locality_critical << "\n";

        out << "Scheduler overhead sensitive: "
            << report.execution.scheduler_overhead_sensitive << "\n";

        out << "Load balancing important: "
            << report.execution.load_balancing_important << "\n";

        out << "ThreadPool score: "
            << report.thread_pool_score << "\n";

        out << "StaticThread score: "
            << report.static_thread_score << "\n";

        out << "OneTbb score: "
            << report.one_tbb_score << "\n";

        out << "Selected engine: "
            << engine_name(report.plan.engine) << "\n";

        std::cout << "Decision source: ";

        switch (report.source)
        {
        case DecisionSource::Analytical:
            std::cout << "Analytical";
            break;

        case DecisionSource::Historical:
            std::cout << "Historical";
            break;
        }

        std::cout << "\n";

        std::cout << "Decision confidence: "
                << report.decision_confidence
                << "\n";
            }
}
