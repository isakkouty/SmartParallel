#include <smart/execution/algorithms.hpp>

namespace smart::detail
{
namespace
{
class AlgorithmSchedulerFunction
{
  public:
    AlgorithmSchedulerFunction(std::size_t callsite_key,
                               AlgorithmChunkFunctionRef function,
                               AlgorithmSequentialRangeFunctionRef sequential_function) noexcept
        : callsite_key_(callsite_key),
          function_(function),
          sequential_function_(sequential_function)
    {
    }

    std::size_t smartparallel_callsite_key() const noexcept { return callsite_key_; }

    void operator()(std::size_t chunk) { function_(chunk); }

    void smartparallel_execute_sequential(std::size_t begin, std::size_t end)
    {
        sequential_function_(begin, end);
    }

  private:
    std::size_t callsite_key_ = 0;
    AlgorithmChunkFunctionRef function_;
    AlgorithmSequentialRangeFunctionRef sequential_function_;
};
} // namespace

void execute_algorithm_chunks(
    std::size_t chunk_count,
    std::size_t callsite_key,
    AlgorithmChunkFunctionRef function,
    AlgorithmSequentialRangeFunctionRef sequential_function)
{
    if (chunk_count == 0)
        return;
    parallel_for(
        std::size_t{0},
        chunk_count,
        AlgorithmSchedulerFunction(callsite_key, function, sequential_function));
}
} // namespace smart::detail
