#include <cassert>
#include <smart/ranking/decision_context.hpp>

int main()
{
    smart::FunctionProfile profile;
    profile.available = true;
    profile.median_ms_per_iteration = 0.25;
    profile.coefficient_of_variation = 0.4;
    profile.tail_ratio = 1.8;
    profile.parallel_worthiness = 0.9;
    profile.regional_cost_ratio = 1.2;

    smart::ExecutionHints hints;
    hints.available = true;
    hints.branchiness = 0.7;
    hints.memory_randomness = 0.3;

    const auto context = smart::ranking::make_decision_context(4096, &profile, &hints);
    assert(context.logical_iterations == 4096);
    assert(context.profile_available);
    assert(context.profile_median_ms_per_iteration == 0.25);
    assert(context.hints.branchiness == 0.7);
    return 0;
}
