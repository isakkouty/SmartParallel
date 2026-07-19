#include <cassert>
#include <cmath>
#include <iostream>
#include <smart/decision/regret_weighted_ranker.hpp>
#include <vector>

int main()
{
    using smart::LinearUtilityModel;
    using smart::PairwiseTrainingExample;
    using smart::RegretWeightedPairwiseTrainer;

    RegretWeightedPairwiseTrainer trainer;

    PairwiseTrainingExample near_tie{{1.0, 0.0}, {0.0, 1.0}, 100.0, 100.2};
    PairwiseTrainingExample catastrophe{{1.0, 0.0}, {0.0, 1.0}, 10.0, 200.0};

    assert(trainer.example_weight(near_tie) == 0.0);
    assert(trainer.example_weight(catastrophe) > 2.9);

    LinearUtilityModel model(2);
    for (int epoch = 0; epoch < 300; ++epoch)
        trainer.train_one(model, catastrophe);

    const std::vector<std::vector<double>> actions{{1.0, 0.0}, {0.0, 1.0}};
    assert(model.choose(actions) == 0);
    assert(model.score(actions[0]) < model.score(actions[1]));

    std::cout << "Phase 1 regret-weighted ranker tests passed.\n";
    return 0;
}
