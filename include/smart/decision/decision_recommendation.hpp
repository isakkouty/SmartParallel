#pragma once

#include <smart/decision/decision_report.hpp>

namespace smart
{
    struct DecisionRecommendation
    {
        DecisionReport report;
        bool available = false;
    };
}
