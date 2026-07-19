#include <cassert>
#include <cmath>
#include <iostream>
#include <smart/validation/regret_metrics.hpp>
#include <vector>

int main()
{
    const auto summary = smart::validation::summarize_regret({0.0, 0.0, 0.10, 0.50}, 0.20);
    assert(std::abs(summary.mean - 0.15) < 1.0e-12);
    assert(std::abs(summary.median - 0.05) < 1.0e-12);
    assert(std::abs(summary.worst - 0.50) < 1.0e-12);
    assert(std::abs(summary.catastrophic_rate - 0.25) < 1.0e-12);
    std::cout << "Phase 1 regret metric tests passed.\n";
}
