#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace smart::validation
{
struct RegretSummary
{
    double mean = 0.0;
    double median = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double worst = 0.0;
    double catastrophic_rate = 0.0;
};

inline double percentile(std::vector<double> values, double probability)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const double position = probability * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    if (lower == upper)
        return values[lower];
    const double fraction = position - static_cast<double>(lower);
    return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

inline RegretSummary summarize_regret(const std::vector<double>& regrets,
                                      double catastrophic_threshold = 0.20)
{
    if (regrets.empty())
        throw std::invalid_argument("regret sample cannot be empty");
    RegretSummary result;
    result.mean = std::accumulate(regrets.begin(), regrets.end(), 0.0) / regrets.size();
    result.median = percentile(regrets, 0.50);
    result.p95 = percentile(regrets, 0.95);
    result.p99 = percentile(regrets, 0.99);
    result.worst = *std::max_element(regrets.begin(), regrets.end());
    result.catastrophic_rate =
        static_cast<double>(std::count_if(regrets.begin(),
                                          regrets.end(),
                                          [catastrophic_threshold](double value)
                                          {
                                              return value > catastrophic_threshold;
                                          }))
        / regrets.size();
    return result;
}
} // namespace smart::validation
