#pragma once

// Compatibility facade for the original experimental include path.
// New code should include smart/ranking/* directly.
#include <smart/ranking/regret_weighted_pairwise_trainer.hpp>

namespace smart
{
    using ranking::LinearUtilityModel;
    using ranking::PairwiseTrainingExample;
    using ranking::RegretWeightedPairwiseTrainer;
}
