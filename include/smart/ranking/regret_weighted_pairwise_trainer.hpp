#pragma once

#include <algorithm>
#include <cmath>
#include <smart/ranking/utility_model.hpp>
#include <stdexcept>
#include <vector>

namespace smart::ranking
{
struct PairwiseTrainingExample
{
    std::vector<double> better_features;
    std::vector<double> worse_features;
    double better_runtime_ms = 0.0;
    double worse_runtime_ms = 0.0;
};

class RegretWeightedPairwiseTrainer
{
  public:
    struct Options
    {
        double learning_rate = 0.03;
        double l2 = 1.0e-4;
        double maximum_weight = 3.0;
        double near_tie_fraction = 0.005;
    };

    RegretWeightedPairwiseTrainer() = default;
    explicit RegretWeightedPairwiseTrainer(Options options)
        : options_(options)
    {
    }

    double example_weight(const PairwiseTrainingExample& example) const
    {
        validate(example);
        const double ratio = example.worse_runtime_ms / example.better_runtime_ms;
        if (ratio <= 1.0 + options_.near_tie_fraction)
            return 0.0;
        return std::min(options_.maximum_weight, std::abs(std::log(ratio)));
    }

    double train_one(LinearUtilityModel& model, const PairwiseTrainingExample& example) const
    {
        validate(example);
        if (example.better_features.size() != model.weights().size()
            || example.worse_features.size() != model.weights().size())
            throw std::invalid_argument("training feature count does not match utility model");

        const double importance = example_weight(example);
        if (importance == 0.0)
            return 0.0;

        const double margin_error =
            model.score(example.better_features) - model.score(example.worse_features);
        const double probability = stable_sigmoid(margin_error);
        const double loss = importance * stable_softplus(margin_error);

        auto& parameters = model.weights();
        for (std::size_t i = 0; i < parameters.size(); ++i)
        {
            const double feature_difference =
                example.better_features[i] - example.worse_features[i];
            const double gradient =
                importance * probability * feature_difference + options_.l2 * parameters[i];
            parameters[i] -= options_.learning_rate * gradient;
        }
        return loss;
    }

  private:
    static double stable_sigmoid(double value)
    {
        if (value >= 0.0)
        {
            const double e = std::exp(-value);
            return 1.0 / (1.0 + e);
        }
        const double e = std::exp(value);
        return e / (1.0 + e);
    }
    static double stable_softplus(double value)
    {
        if (value > 30.0)
            return value;
        if (value < -30.0)
            return std::exp(value);
        return std::log1p(std::exp(value));
    }
    static void validate(const PairwiseTrainingExample& example)
    {
        if (example.better_runtime_ms <= 0.0 || example.worse_runtime_ms <= 0.0)
            throw std::invalid_argument("pairwise runtimes must be positive");
        if (example.better_runtime_ms > example.worse_runtime_ms)
            throw std::invalid_argument("better runtime must not exceed worse runtime");
        if (example.better_features.size() != example.worse_features.size())
            throw std::invalid_argument("pairwise feature vectors must have equal length");
    }
    Options options_;
};
} // namespace smart::ranking
