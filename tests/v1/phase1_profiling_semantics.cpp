#include <smart/profiling/isolated_function_profile.hpp>

#include <cassert>
#include <memory>
#include <vector>

namespace
{
    struct NonCopyableValue
    {
        NonCopyableValue() = default;
        NonCopyableValue(const NonCopyableValue&) = delete;
        NonCopyableValue& operator=(const NonCopyableValue&) = delete;
    };

    struct NonCopyableCallable
    {
        NonCopyableCallable() = default;
        NonCopyableCallable(const NonCopyableCallable&) = delete;

        void operator()(int& value)
        {
            ++value;
        }
    };
}

int main()
{
    smart::FunctionProfiler::Config config;
    config.min_samples = 2;
    config.max_samples = 4;
    config.batch_size = 2;

    std::vector<int> values{1, 2, 3, 4, 5, 6};
    const std::vector<int> original = values;

    const smart::FunctionProfile profile =
        smart::profile_container_on_copies(
            values,
            [](int& value)
            {
                value += 10;
            },
            config);

    assert(profile.available);
    assert(
        profile.sampling_mode ==
        smart::FunctionProfileSamplingMode::IsolatedCopies);
    assert(values == original);

    std::vector<int> left{1, 2, 3};
    std::vector<int> right{4, 5, 6};
    const std::vector<int> original_left = left;
    const std::vector<int> original_right = right;

    const smart::FunctionProfile pair_profile =
        smart::profile_pair_on_copies(
            left,
            right,
            [](int& a, int& b)
            {
                a += b;
                b += a;
            },
            config);

    assert(pair_profile.available);
    assert(left == original_left);
    assert(right == original_right);

    std::vector<NonCopyableValue> non_copyable_values(2);

    const smart::FunctionProfile non_copyable_value_profile =
        smart::profile_container_on_copies(
            non_copyable_values,
            [](NonCopyableValue&)
            {
            },
            config);

    assert(!non_copyable_value_profile.available);
    assert(
        non_copyable_value_profile.unavailable_reason ==
        smart::FunctionProfileUnavailableReason::ValueNotCopyConstructible);

    NonCopyableCallable non_copyable_callable;

    const smart::FunctionProfile non_copyable_callable_profile =
        smart::profile_container_on_copies(
            values,
            non_copyable_callable,
            config);

    assert(!non_copyable_callable_profile.available);
    assert(
        non_copyable_callable_profile.unavailable_reason ==
        smart::FunctionProfileUnavailableReason::CallableNotCopyConstructible);

    return 0;
}
