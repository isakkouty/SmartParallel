#pragma once

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace smart::ranking
{
    // Shared latent utility model. Lower utility means a better action.
    // Scores are ordinal decision values and must never be interpreted as time.
    class LinearUtilityModel
    {
    public:
        explicit LinearUtilityModel(std::size_t feature_count = 0)
            : weights_(feature_count, 0.0)
        {
        }

        double score(const std::vector<double>& features) const
        {
            if (features.size() != weights_.size())
                throw std::invalid_argument("feature count does not match utility model");
            double value = 0.0;
            for (std::size_t i = 0; i < weights_.size(); ++i)
                value += weights_[i] * features[i];
            return value;
        }

        std::size_t choose(const std::vector<std::vector<double>>& candidates) const
        {
            if (candidates.empty())
                throw std::invalid_argument("cannot choose from an empty candidate set");
            std::size_t best_index = 0;
            double best_utility = std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < candidates.size(); ++i)
            {
                const double utility = score(candidates[i]);
                if (utility < best_utility)
                {
                    best_utility = utility;
                    best_index = i;
                }
            }
            return best_index;
        }

        std::vector<double>& weights() noexcept { return weights_; }
        const std::vector<double>& weights() const noexcept { return weights_; }

    private:
        std::vector<double> weights_;
    };
}
