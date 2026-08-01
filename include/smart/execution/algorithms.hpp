#pragma once

#include <smart/execution/algorithm_dispatch.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace smart
{
namespace detail
{
class AlgorithmChunkFunctionRef
{
  public:
    template <typename Function,
              typename = std::enable_if_t<
                  !std::is_same_v<std::decay_t<Function>, AlgorithmChunkFunctionRef>>>
    explicit AlgorithmChunkFunctionRef(Function& function) noexcept
        : object_(std::addressof(function)),
          invoke_([](void* object, std::size_t chunk)
          {
              (*static_cast<Function*>(object))(chunk);
          })
    {
    }

    void operator()(std::size_t chunk) const { invoke_(object_, chunk); }

  private:
    void* object_ = nullptr;
    void (*invoke_)(void*, std::size_t) = nullptr;
};

class AlgorithmSequentialRangeFunctionRef
{
  public:
    template <typename Function,
              typename = std::enable_if_t<!std::is_same_v<
                  std::decay_t<Function>, AlgorithmSequentialRangeFunctionRef>>>
    explicit AlgorithmSequentialRangeFunctionRef(Function& function) noexcept
        : object_(std::addressof(function)),
          invoke_([](void* object, std::size_t begin, std::size_t end)
          {
              (*static_cast<Function*>(object))(begin, end);
          })
    {
    }

    void operator()(std::size_t begin, std::size_t end) const
    {
        invoke_(object_, begin, end);
    }

  private:
    void* object_ = nullptr;
    void (*invoke_)(void*, std::size_t, std::size_t) = nullptr;
};

// Implemented in src/algorithms.cpp. Keeping the scheduler invocation behind a
// fixed callback type avoids repeatedly instantiating the large parallel_for
// template for every public algorithm/callable combination. Parallel plans
// invoke the chunk callback. Scheduler-approved sequential plans invoke the
// direct range callback, eliminating chunk dispatch and synchronization costs.
void execute_algorithm_chunks(
    std::size_t chunk_count,
    std::size_t callsite_key,
    AlgorithmChunkFunctionRef function,
    AlgorithmSequentialRangeFunctionRef sequential_function);

template <typename Iterator>
using IteratorCategory = typename std::iterator_traits<Iterator>::iterator_category;

template <typename Iterator>
constexpr bool is_random_access_iterator_v =
    std::is_base_of_v<std::random_access_iterator_tag, IteratorCategory<Iterator>>;

template <typename Iterator>
std::size_t algorithm_range_size(Iterator first, Iterator last, const char* algorithm_name)
{
    static_assert(is_random_access_iterator_v<Iterator>,
                  "SmartParallel v1.4 algorithms require random-access iterators");
    const auto distance = last - first;
    if (distance < 0)
        throw std::invalid_argument(std::string("SmartParallel ") + algorithm_name
                                    + " end must not precede begin");
    using UnsignedDifference =
        std::make_unsigned_t<std::remove_cv_t<decltype(distance)>>;
    const auto unsigned_distance = static_cast<UnsignedDifference>(distance);
    if (unsigned_distance > std::numeric_limits<std::size_t>::max())
        throw std::overflow_error(std::string("SmartParallel ") + algorithm_name
                                  + " range size overflow");
    return static_cast<std::size_t>(unsigned_distance);
}

template <typename Iterator>
Iterator algorithm_advance(Iterator iterator, std::size_t offset)
{
    using Difference = typename std::iterator_traits<Iterator>::difference_type;
    if (offset > static_cast<std::size_t>(std::numeric_limits<Difference>::max()))
        throw std::overflow_error("SmartParallel iterator offset overflow");
    return iterator + static_cast<Difference>(offset);
}

inline std::size_t algorithm_identity(ParallelAlgorithmKind kind) noexcept
{
    return combine_hash(0x5331504152414C47ull, static_cast<std::size_t>(kind));
}

template <typename Callable>
std::size_t algorithm_identity(ParallelAlgorithmKind kind, const Callable& callable) noexcept
{
    return combine_hash(algorithm_identity(kind), callable_identity_hash(callable));
}

template <typename CallableA, typename CallableB>
std::size_t algorithm_identity(ParallelAlgorithmKind kind,
                               const CallableA& first,
                               const CallableB& second) noexcept
{
    return combine_hash(
        combine_hash(algorithm_identity(kind), callable_identity_hash(first)),
        callable_identity_hash(second));
}

template <typename Value>
std::size_t typed_algorithm_identity(ParallelAlgorithmKind kind) noexcept
{
    return combine_hash(algorithm_identity(kind), typeid(Value).hash_code());
}

inline std::size_t algorithm_chunk_count(std::size_t total)
{
    if (total == 0)
        return 0;
    const HardwareCharacteristics hardware = hardware_characteristics();
    std::size_t workers = std::max<std::size_t>(1, hardware.logical_threads);
    const Config& config = global_config();
    const ExecutionContext context = current_execution_context();
    if (context.depth > 0)
    {
        const std::size_t inherited_budget = context.parallel
            ? context.concurrency_budget
            : context.inherited_concurrency_budget;
        workers = std::min(workers, std::max<std::size_t>(1, inherited_budget));
    }
    else if (config.nested_root_concurrency_budget != 0)
    {
        workers = std::min(workers, config.nested_root_concurrency_budget);
    }
    const std::size_t target_chunks_per_worker = std::max<std::size_t>(
        4, config.nested_target_chunks_per_worker);
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    const std::size_t desired = workers > maximum / target_chunks_per_worker
        ? maximum
        : workers * target_chunks_per_worker;
    return std::max<std::size_t>(1, std::min(total, desired));
}

struct AlgorithmChunkRange
{
    std::size_t begin = 0;
    std::size_t end = 0;
};

template <typename Function>
class AlgorithmChunkCopies
{
  public:
    AlgorithmChunkCopies(const Function& function, std::size_t chunk_count)
        : function_(function), chunk_count_(chunk_count)
    {
    }

    Function& operator[](std::size_t chunk)
    {
        std::call_once(initialization_, [&]
        {
            functions_.reserve(chunk_count_);
            for (std::size_t index = 0; index < chunk_count_; ++index)
                functions_.emplace_back(function_);
        });
        return functions_[chunk];
    }

  private:
    Function function_;
    std::size_t chunk_count_ = 0;
    std::once_flag initialization_;
    std::vector<Function> functions_;
};

inline AlgorithmChunkRange algorithm_chunk_range(std::size_t total,
                                                  std::size_t chunks,
                                                  std::size_t ordinal) noexcept
{
    const std::size_t base = total / chunks;
    const std::size_t remainder = total % chunks;
    const std::size_t begin = ordinal * base + std::min(ordinal, remainder);
    const std::size_t length = base + (ordinal < remainder ? 1u : 0u);
    return {begin, begin + length};
}

template <typename Function, typename SequentialFunction>
void run_chunked_algorithm(std::size_t total,
                           std::size_t callsite_key,
                           Function&& function,
                           SequentialFunction&& sequential_function)
{
    using ChunkFunction = std::decay_t<Function>;
    static_assert(std::is_copy_constructible_v<ChunkFunction>,
                  "SmartParallel algorithm callables must be copy constructible");
    if (total == 0)
        return;
    const std::size_t chunks = algorithm_chunk_count(total);
    AlgorithmChunkCopies<ChunkFunction> chunk_functions(function, chunks);
    auto chunk_function = [&](std::size_t chunk)
    {
        chunk_functions[chunk](algorithm_chunk_range(total, chunks, chunk));
    };
    auto serial_range_function = [&](std::size_t chunk_begin, std::size_t chunk_end)
    {
        if (chunk_begin == 0 && chunk_end == chunks)
        {
            sequential_function();
            return;
        }
        for (std::size_t chunk = chunk_begin; chunk < chunk_end; ++chunk)
            chunk_function(chunk);
    };
    execute_algorithm_chunks(
        chunks,
        callsite_key,
        AlgorithmChunkFunctionRef(chunk_function),
        AlgorithmSequentialRangeFunctionRef(serial_range_function));
}

template <typename Function>
void run_chunked_algorithm(std::size_t total,
                           std::size_t callsite_key,
                           Function&& function)
{
    using ChunkFunction = std::decay_t<Function>;
    ChunkFunction sequential_function(function);
    run_chunked_algorithm(
        total,
        callsite_key,
        std::forward<Function>(function),
        [total, sequential_function = std::move(sequential_function)]() mutable
        {
            sequential_function(AlgorithmChunkRange{0, total});
        });
}

template <typename InputIterator,
          typename T,
          typename BinaryOperation,
          typename Transform,
          typename SequentialFunction>
T chunked_transform_reduce_with_sequential(ParallelAlgorithmKind kind,
                                           InputIterator first,
                                           std::size_t total,
                                           T init,
                                           BinaryOperation binary_operation,
                                           Transform transform,
                                           SequentialFunction sequential_function,
                                           std::size_t scheduler_callsite = 0)
{
    static_assert(std::is_copy_constructible_v<BinaryOperation>,
                  "SmartParallel reduction operations must be copy constructible");
    static_assert(std::is_copy_constructible_v<Transform>,
                  "SmartParallel transform operations must be copy constructible");
    if (total == 0)
        return init;

    const std::size_t chunks = algorithm_chunk_count(total);
    std::vector<std::optional<T>> partials;
    std::once_flag partials_initialization;
    const std::size_t callsite = scheduler_callsite == 0
        ? algorithm_identity(kind, binary_operation, transform)
        : scheduler_callsite;
    auto chunk_function = [=, &partials, &partials_initialization](std::size_t chunk) mutable
    {
        std::call_once(partials_initialization, [&] { partials.resize(chunks); });
        const AlgorithmChunkRange range = algorithm_chunk_range(total, chunks, chunk);
        BinaryOperation local_binary = binary_operation;
        Transform local_transform = transform;
        T partial = std::invoke(local_transform, *algorithm_advance(first, range.begin));
        for (std::size_t index = range.begin + 1; index < range.end; ++index)
        {
            partial = std::invoke(
                local_binary,
                std::move(partial),
                std::invoke(local_transform, *algorithm_advance(first, index)));
        }
        partials[chunk].emplace(std::move(partial));
    };

    bool direct_sequential = false;
    auto direct_function = [&]() mutable
    {
        init = std::invoke(sequential_function, std::move(init));
        direct_sequential = true;
    };
    auto serial_range_function = [&](std::size_t chunk_begin, std::size_t chunk_end)
    {
        if (chunk_begin == 0 && chunk_end == chunks)
        {
            direct_function();
            return;
        }
        for (std::size_t chunk = chunk_begin; chunk < chunk_end; ++chunk)
            chunk_function(chunk);
    };
    execute_algorithm_chunks(
        chunks,
        callsite,
        AlgorithmChunkFunctionRef(chunk_function),
        AlgorithmSequentialRangeFunctionRef(serial_range_function));

    if (direct_sequential)
        return init;
    for (std::size_t chunk = 0; chunk < chunks; ++chunk)
        init = std::invoke(binary_operation, std::move(init), std::move(partials[chunk].value()));
    return init;
}

template <typename InputIterator,
          typename T,
          typename BinaryOperation,
          typename Transform>
T chunked_transform_reduce(ParallelAlgorithmKind kind,
                           InputIterator first,
                           std::size_t total,
                           T init,
                           BinaryOperation binary_operation,
                           Transform transform)
{
    auto sequential_function = [=](T result) mutable
    {
        BinaryOperation local_binary = binary_operation;
        Transform local_transform = transform;
        InputIterator iterator = first;
        for (std::size_t index = 0; index < total; ++index, ++iterator)
        {
            result = std::invoke(
                local_binary,
                std::move(result),
                std::invoke(local_transform, *iterator));
        }
        return result;
    };
    return chunked_transform_reduce_with_sequential(
        kind,
        first,
        total,
        std::move(init),
        std::move(binary_operation),
        std::move(transform),
        std::move(sequential_function));
}

template <typename InputIterator1,
          typename InputIterator2,
          typename T,
          typename BinaryOperation,
          typename Transform>
T chunked_binary_transform_reduce(ParallelAlgorithmKind kind,
                                  InputIterator1 first1,
                                  InputIterator2 first2,
                                  std::size_t total,
                                  T init,
                                  BinaryOperation binary_operation,
                                  Transform transform)
{
    static_assert(std::is_copy_constructible_v<BinaryOperation>,
                  "SmartParallel reduction operations must be copy constructible");
    static_assert(std::is_copy_constructible_v<Transform>,
                  "SmartParallel transform operations must be copy constructible");
    if (total == 0)
        return init;

    const std::size_t chunks = algorithm_chunk_count(total);
    std::vector<std::optional<T>> partials;
    std::once_flag partials_initialization;
    const std::size_t callsite = algorithm_identity(kind, binary_operation, transform);
    auto chunk_function = [=, &partials, &partials_initialization](std::size_t chunk) mutable
    {
        std::call_once(partials_initialization, [&] { partials.resize(chunks); });
        const AlgorithmChunkRange range = algorithm_chunk_range(total, chunks, chunk);
        BinaryOperation local_binary = binary_operation;
        Transform local_transform = transform;
        T partial = std::invoke(
            local_transform,
            *algorithm_advance(first1, range.begin),
            *algorithm_advance(first2, range.begin));
        for (std::size_t index = range.begin + 1; index < range.end; ++index)
        {
            partial = std::invoke(
                local_binary,
                std::move(partial),
                std::invoke(
                    local_transform,
                    *algorithm_advance(first1, index),
                    *algorithm_advance(first2, index)));
        }
        partials[chunk].emplace(std::move(partial));
    };

    bool direct_sequential = false;
    auto sequential_function = [&]() mutable
    {
        BinaryOperation local_binary = binary_operation;
        Transform local_transform = transform;
        InputIterator1 iterator1 = first1;
        InputIterator2 iterator2 = first2;
        for (std::size_t index = 0; index < total;
             ++index, ++iterator1, ++iterator2)
        {
            init = std::invoke(
                local_binary,
                std::move(init),
                std::invoke(local_transform, *iterator1, *iterator2));
        }
        direct_sequential = true;
    };
    auto serial_range_function = [&](std::size_t chunk_begin, std::size_t chunk_end)
    {
        if (chunk_begin == 0 && chunk_end == chunks)
        {
            sequential_function();
            return;
        }
        for (std::size_t chunk = chunk_begin; chunk < chunk_end; ++chunk)
            chunk_function(chunk);
    };
    execute_algorithm_chunks(
        chunks,
        callsite,
        AlgorithmChunkFunctionRef(chunk_function),
        AlgorithmSequentialRangeFunctionRef(serial_range_function));

    if (direct_sequential)
        return init;
    for (std::size_t chunk = 0; chunk < chunks; ++chunk)
        init = std::invoke(binary_operation, std::move(init), std::move(partials[chunk].value()));
    return init;
}

template <typename InputIterator, typename Predicate>
std::size_t parallel_find_index(ParallelAlgorithmKind kind,
                                InputIterator first,
                                std::size_t total,
                                Predicate predicate,
                                std::size_t scheduler_callsite = 0)
{
    if (total == 0)
        return 0;

    static_assert(std::is_copy_constructible_v<Predicate>,
                  "SmartParallel search predicates must be copy constructible");
    std::atomic<std::size_t> best{total};
    const std::size_t callsite = scheduler_callsite == 0
        ? algorithm_identity(kind, predicate)
        : scheduler_callsite;
    const std::size_t chunks = algorithm_chunk_count(total);
    AlgorithmChunkCopies<Predicate> predicates(predicate, chunks);
    auto chunk_function = [=, &best, &predicates](std::size_t chunk) mutable
    {
        const AlgorithmChunkRange range = algorithm_chunk_range(total, chunks, chunk);
        if (range.begin >= best.load(std::memory_order_relaxed))
            return;
        Predicate& local_predicate = predicates[chunk];
        for (std::size_t index = range.begin; index < range.end; ++index)
        {
            if (index >= best.load(std::memory_order_relaxed))
                break;
            if (!std::invoke(local_predicate, *algorithm_advance(first, index)))
                continue;
            std::size_t observed = best.load(std::memory_order_relaxed);
            while (index < observed
                   && !best.compare_exchange_weak(
                       observed,
                       index,
                       std::memory_order_relaxed,
                       std::memory_order_relaxed))
            {
            }
            break;
        }
    };
    auto sequential_function = [&]() mutable
    {
        Predicate local_predicate = predicate;
        const InputIterator last = algorithm_advance(first, total);
        const InputIterator found = std::find_if(first, last, local_predicate);
        best.store(static_cast<std::size_t>(found - first), std::memory_order_relaxed);
    };
    auto serial_range_function = [&](std::size_t chunk_begin, std::size_t chunk_end)
    {
        if (chunk_begin == 0 && chunk_end == chunks)
        {
            sequential_function();
            return;
        }
        for (std::size_t chunk = chunk_begin; chunk < chunk_end; ++chunk)
            chunk_function(chunk);
    };
    execute_algorithm_chunks(
        chunks,
        callsite,
        AlgorithmChunkFunctionRef(chunk_function),
        AlgorithmSequentialRangeFunctionRef(serial_range_function));
    return best.load(std::memory_order_relaxed);
}

} // namespace detail

// v1.4 deliberately requires random-access iterators. That keeps every API on
// the existing indexed scheduler and avoids introducing a second partitioning
// or traversal execution model.
template <typename RandomAccessIterator, typename Function>
void parallel_for_each(RandomAccessIterator first, RandomAccessIterator last, Function function)
{
    const std::size_t total = detail::algorithm_range_size(first, last, "parallel_for_each");
    const std::size_t callsite =
        detail::algorithm_identity(detail::ParallelAlgorithmKind::ForEach, function);
    detail::run_chunked_algorithm(
        total,
        callsite,
        [=](detail::AlgorithmChunkRange range) mutable
        {
            for (std::size_t index = range.begin; index < range.end; ++index)
                std::invoke(function, *detail::algorithm_advance(first, index));
        },
        [=]() mutable
        {
            std::for_each(first, last, function);
        });
}

template <typename InputIterator, typename OutputIterator, typename UnaryOperation>
OutputIterator parallel_transform(InputIterator first,
                                  InputIterator last,
                                  OutputIterator output,
                                  UnaryOperation operation)
{
    static_assert(detail::is_random_access_iterator_v<OutputIterator>,
                  "SmartParallel parallel_transform requires a random-access output iterator");
    const std::size_t total = detail::algorithm_range_size(first, last, "parallel_transform");
    const std::size_t callsite =
        detail::algorithm_identity(detail::ParallelAlgorithmKind::Transform, operation);
    detail::run_chunked_algorithm(
        total,
        callsite,
        [=](detail::AlgorithmChunkRange range) mutable
        {
            for (std::size_t index = range.begin; index < range.end; ++index)
            {
                *detail::algorithm_advance(output, index) =
                    std::invoke(operation, *detail::algorithm_advance(first, index));
            }
        },
        [=]() mutable
        {
            std::transform(first, last, output, operation);
        });
    return detail::algorithm_advance(output, total);
}

template <typename InputIterator1,
          typename InputIterator2,
          typename OutputIterator,
          typename BinaryOperation>
OutputIterator parallel_transform(InputIterator1 first1,
                                  InputIterator1 last1,
                                  InputIterator2 first2,
                                  OutputIterator output,
                                  BinaryOperation operation)
{
    static_assert(detail::is_random_access_iterator_v<InputIterator2>,
                  "SmartParallel parallel_transform requires random-access input iterators");
    static_assert(detail::is_random_access_iterator_v<OutputIterator>,
                  "SmartParallel parallel_transform requires a random-access output iterator");
    const std::size_t total = detail::algorithm_range_size(first1, last1, "parallel_transform");
    const std::size_t callsite =
        detail::algorithm_identity(detail::ParallelAlgorithmKind::Transform, operation);
    detail::run_chunked_algorithm(
        total,
        callsite,
        [=](detail::AlgorithmChunkRange range) mutable
        {
            for (std::size_t index = range.begin; index < range.end; ++index)
            {
                *detail::algorithm_advance(output, index) = std::invoke(
                    operation,
                    *detail::algorithm_advance(first1, index),
                    *detail::algorithm_advance(first2, index));
            }
        },
        [=]() mutable
        {
            std::transform(first1, last1, first2, output, operation);
        });
    return detail::algorithm_advance(output, total);
}

template <typename InputIterator, typename OutputIterator>
OutputIterator parallel_copy(InputIterator first, InputIterator last, OutputIterator output)
{
    static_assert(detail::is_random_access_iterator_v<OutputIterator>,
                  "SmartParallel parallel_copy requires a random-access output iterator");
    const std::size_t total = detail::algorithm_range_size(first, last, "parallel_copy");
    if (total == 0)
        return output;

    using Value = typename std::iterator_traits<InputIterator>::value_type;
    std::size_t callsite =
        detail::typed_algorithm_identity<Value>(detail::ParallelAlgorithmKind::Copy);
    callsite = detail::combine_hash(callsite, typeid(InputIterator).hash_code());
    callsite = detail::combine_hash(callsite, typeid(OutputIterator).hash_code());

    auto sequential_function = [=]() mutable
    {
        std::copy(first, last, output);
    };
    auto scheduled_function = [=](std::size_t scheduler_callsite) mutable
    {
        detail::run_chunked_algorithm(
            total,
            scheduler_callsite,
            [=](detail::AlgorithmChunkRange range) mutable
            {
                const InputIterator chunk_first = detail::algorithm_advance(first, range.begin);
                const InputIterator chunk_last = detail::algorithm_advance(first, range.end);
                const OutputIterator chunk_output = detail::algorithm_advance(output, range.begin);
                std::copy(chunk_first, chunk_last, chunk_output);
            },
            [=]() mutable
            {
                std::copy(first, last, output);
            });
    };
    detail::run_hot_dispatched_algorithm(
        detail::ParallelAlgorithmKind::Copy,
        callsite,
        total,
        sizeof(Value),
        sequential_function,
        scheduled_function);
    return detail::algorithm_advance(output, total);
}

template <typename RandomAccessIterator, typename T>
void parallel_fill(RandomAccessIterator first, RandomAccessIterator last, const T& value)
{
    using FillValue = std::decay_t<T>;
    static_assert(std::is_copy_constructible_v<FillValue>,
                  "SmartParallel parallel_fill requires a copy-constructible value");
    const std::size_t total = detail::algorithm_range_size(first, last, "parallel_fill");
    const std::size_t callsite =
        detail::typed_algorithm_identity<FillValue>(detail::ParallelAlgorithmKind::Fill);
    FillValue fill_value(value);
    detail::run_chunked_algorithm(
        total,
        callsite,
        [first, fill_value](detail::AlgorithmChunkRange range)
        {
            for (std::size_t index = range.begin; index < range.end; ++index)
                *detail::algorithm_advance(first, index) = fill_value;
        },
        [first, last, fill_value]() mutable
        {
            std::fill(first, last, fill_value);
        });
}

// The generator receives the zero-based logical offset. This deterministic
// indexed contract avoids races and sequence changes from a shared stateful
// no-argument generator.
template <typename RandomAccessIterator, typename Generator>
void parallel_generate(RandomAccessIterator first, RandomAccessIterator last, Generator generator)
{
    static_assert(std::is_invocable_v<Generator&, std::size_t>,
                  "SmartParallel parallel_generate requires generator(index)");
    const std::size_t total = detail::algorithm_range_size(first, last, "parallel_generate");
    const std::size_t callsite =
        detail::algorithm_identity(detail::ParallelAlgorithmKind::Generate, generator);
    detail::run_chunked_algorithm(
        total,
        callsite,
        [=](detail::AlgorithmChunkRange range) mutable
        {
            for (std::size_t index = range.begin; index < range.end; ++index)
                *detail::algorithm_advance(first, index) = std::invoke(generator, index);
        },
        [=]() mutable
        {
            RandomAccessIterator iterator = first;
            for (std::size_t index = 0; index < total; ++index, ++iterator)
                *iterator = std::invoke(generator, index);
        });
}

template <typename InputIterator, typename T, typename BinaryOperation>
T parallel_reduce(InputIterator first,
                  InputIterator last,
                  T init,
                  BinaryOperation binary_operation)
{
    const std::size_t total = detail::algorithm_range_size(first, last, "parallel_reduce");
    if (total == 0)
        return init;

    using InputValue = typename std::iterator_traits<InputIterator>::value_type;
    const auto identity = [](const auto& value) -> T { return static_cast<T>(value); };
    const std::size_t callsite = detail::algorithm_identity(
        detail::ParallelAlgorithmKind::Reduce, binary_operation, identity);

    auto sequential_route = [&]() mutable
    {
        return std::accumulate(first, last, std::move(init), binary_operation);
    };
    auto scheduled_route = [&](std::size_t scheduler_callsite) mutable
    {
        auto scheduler_sequential = [=](T result) mutable
        {
            return std::accumulate(first, last, std::move(result), binary_operation);
        };
        return detail::chunked_transform_reduce_with_sequential(
            detail::ParallelAlgorithmKind::Reduce,
            first,
            total,
            std::move(init),
            std::move(binary_operation),
            identity,
            std::move(scheduler_sequential),
            scheduler_callsite);
    };
    return detail::run_hot_dispatched_algorithm(
        detail::ParallelAlgorithmKind::Reduce,
        callsite,
        total,
        sizeof(InputValue),
        sequential_route,
        scheduled_route);
}

template <typename InputIterator, typename T>
T parallel_reduce(InputIterator first, InputIterator last, T init)
{
    return parallel_reduce(first, last, std::move(init), std::plus<>{});
}

template <typename InputIterator,
          typename T,
          typename BinaryOperation,
          typename UnaryOperation>
T parallel_transform_reduce(InputIterator first,
                            InputIterator last,
                            T init,
                            BinaryOperation binary_operation,
                            UnaryOperation unary_operation)
{
    const std::size_t total =
        detail::algorithm_range_size(first, last, "parallel_transform_reduce");
    auto sequential_function = [=](T result) mutable
    {
        return std::transform_reduce(
            first, last, std::move(result), binary_operation, unary_operation);
    };
    return detail::chunked_transform_reduce_with_sequential(
        detail::ParallelAlgorithmKind::TransformReduce,
        first,
        total,
        std::move(init),
        std::move(binary_operation),
        std::move(unary_operation),
        std::move(sequential_function));
}

template <typename InputIterator1,
          typename InputIterator2,
          typename T,
          typename BinaryOperation,
          typename BinaryTransform>
T parallel_transform_reduce(InputIterator1 first1,
                            InputIterator1 last1,
                            InputIterator2 first2,
                            T init,
                            BinaryOperation binary_operation,
                            BinaryTransform binary_transform)
{
    static_assert(detail::is_random_access_iterator_v<InputIterator2>,
                  "SmartParallel parallel_transform_reduce requires random-access iterators");
    const std::size_t total =
        detail::algorithm_range_size(first1, last1, "parallel_transform_reduce");
    return detail::chunked_binary_transform_reduce(
        detail::ParallelAlgorithmKind::TransformReduce,
        first1,
        first2,
        total,
        std::move(init),
        std::move(binary_operation),
        std::move(binary_transform));
}

template <typename InputIterator, typename Predicate>
std::size_t parallel_count_if(InputIterator first, InputIterator last, Predicate predicate)
{
    const std::size_t total = detail::algorithm_range_size(first, last, "parallel_count_if");
    const auto transform = [predicate](const auto& value) mutable -> std::size_t
    {
        return std::invoke(predicate, value) ? 1u : 0u;
    };
    auto sequential_function = [=](std::size_t result) mutable
    {
        return result + static_cast<std::size_t>(std::count_if(first, last, predicate));
    };
    return detail::chunked_transform_reduce_with_sequential(
        detail::ParallelAlgorithmKind::CountIf,
        first,
        total,
        std::size_t{0},
        std::plus<>{},
        transform,
        std::move(sequential_function));
}

template <typename InputIterator, typename T>
std::size_t parallel_count(InputIterator first, InputIterator last, const T& value)
{
    const std::size_t total = detail::algorithm_range_size(first, last, "parallel_count");
    if (total == 0)
        return 0;

    using InputValue = typename std::iterator_traits<InputIterator>::value_type;
    using CountValue = std::decay_t<T>;
    static_assert(std::is_copy_constructible_v<CountValue>,
                  "SmartParallel parallel_count requires a copy-constructible value");
    const CountValue count_value(value);
    std::size_t callsite =
        detail::typed_algorithm_identity<CountValue>(detail::ParallelAlgorithmKind::Count);
    callsite = detail::combine_hash(callsite, typeid(InputIterator).hash_code());

    auto sequential_route = [=]()
    {
        return static_cast<std::size_t>(std::count(first, last, count_value));
    };
    auto scheduled_route = [=](std::size_t scheduler_callsite) mutable
    {
        const auto transform = [count_value](const auto& item) -> std::size_t
        {
            return item == count_value ? 1u : 0u;
        };
        auto scheduler_sequential = [=](std::size_t result)
        {
            return result + static_cast<std::size_t>(std::count(first, last, count_value));
        };
        return detail::chunked_transform_reduce_with_sequential(
            detail::ParallelAlgorithmKind::Count,
            first,
            total,
            std::size_t{0},
            std::plus<>{},
            transform,
            std::move(scheduler_sequential),
            scheduler_callsite);
    };
    return detail::run_hot_dispatched_algorithm(
        detail::ParallelAlgorithmKind::Count,
        callsite,
        total,
        sizeof(InputValue),
        sequential_route,
        scheduled_route);
}

template <typename InputIterator, typename Predicate>
bool parallel_any_of(InputIterator first, InputIterator last, Predicate predicate)
{
    const std::size_t total = detail::algorithm_range_size(first, last, "parallel_any_of");
    if (total == 0)
        return false;

    using InputValue = typename std::iterator_traits<InputIterator>::value_type;
    const std::size_t callsite =
        detail::algorithm_identity(detail::ParallelAlgorithmKind::AnyOf, predicate);
    auto sequential_route = [=]() mutable
    {
        return std::any_of(first, last, predicate);
    };
    auto scheduled_route = [=](std::size_t scheduler_callsite) mutable
    {
        std::atomic<bool> result{false};
        detail::run_chunked_algorithm(
            total,
            scheduler_callsite,
            [=, &result](detail::AlgorithmChunkRange range) mutable
            {
                for (std::size_t index = range.begin; index < range.end; ++index)
                {
                    if (result.load(std::memory_order_relaxed))
                        break;
                    if (std::invoke(predicate, *detail::algorithm_advance(first, index)))
                    {
                        result.store(true, std::memory_order_relaxed);
                        break;
                    }
                }
            },
            [=, &result]() mutable
            {
                result.store(std::any_of(first, last, predicate), std::memory_order_relaxed);
            });
        return result.load(std::memory_order_relaxed);
    };
    return detail::run_hot_dispatched_algorithm(
        detail::ParallelAlgorithmKind::AnyOf,
        callsite,
        total,
        sizeof(InputValue),
        sequential_route,
        scheduled_route);
}

template <typename InputIterator, typename Predicate>
bool parallel_all_of(InputIterator first, InputIterator last, Predicate predicate)
{
    const std::size_t total = detail::algorithm_range_size(first, last, "parallel_all_of");
    if (total == 0)
        return true;

    using InputValue = typename std::iterator_traits<InputIterator>::value_type;
    const std::size_t callsite =
        detail::algorithm_identity(detail::ParallelAlgorithmKind::AllOf, predicate);
    auto sequential_route = [=]() mutable
    {
        return std::all_of(first, last, predicate);
    };
    auto scheduled_route = [=](std::size_t scheduler_callsite) mutable
    {
        std::atomic<bool> result{true};
        detail::run_chunked_algorithm(
            total,
            scheduler_callsite,
            [=, &result](detail::AlgorithmChunkRange range) mutable
            {
                for (std::size_t index = range.begin; index < range.end; ++index)
                {
                    if (!result.load(std::memory_order_relaxed))
                        break;
                    if (!std::invoke(predicate, *detail::algorithm_advance(first, index)))
                    {
                        result.store(false, std::memory_order_relaxed);
                        break;
                    }
                }
            },
            [=, &result]() mutable
            {
                result.store(std::all_of(first, last, predicate), std::memory_order_relaxed);
            });
        return result.load(std::memory_order_relaxed);
    };
    return detail::run_hot_dispatched_algorithm(
        detail::ParallelAlgorithmKind::AllOf,
        callsite,
        total,
        sizeof(InputValue),
        sequential_route,
        scheduled_route);
}

template <typename InputIterator, typename Predicate>
bool parallel_none_of(InputIterator first, InputIterator last, Predicate predicate)
{
    const std::size_t total = detail::algorithm_range_size(first, last, "parallel_none_of");
    if (total == 0)
        return true;

    using InputValue = typename std::iterator_traits<InputIterator>::value_type;
    const std::size_t callsite =
        detail::algorithm_identity(detail::ParallelAlgorithmKind::NoneOf, predicate);
    auto sequential_route = [=]() mutable
    {
        return std::none_of(first, last, predicate);
    };
    auto scheduled_route = [=](std::size_t scheduler_callsite) mutable
    {
        std::atomic<bool> result{true};
        detail::run_chunked_algorithm(
            total,
            scheduler_callsite,
            [=, &result](detail::AlgorithmChunkRange range) mutable
            {
                for (std::size_t index = range.begin; index < range.end; ++index)
                {
                    if (!result.load(std::memory_order_relaxed))
                        break;
                    if (std::invoke(predicate, *detail::algorithm_advance(first, index)))
                    {
                        result.store(false, std::memory_order_relaxed);
                        break;
                    }
                }
            },
            [=, &result]() mutable
            {
                result.store(std::none_of(first, last, predicate), std::memory_order_relaxed);
            });
        return result.load(std::memory_order_relaxed);
    };
    return detail::run_hot_dispatched_algorithm(
        detail::ParallelAlgorithmKind::NoneOf,
        callsite,
        total,
        sizeof(InputValue),
        sequential_route,
        scheduled_route);
}

template <typename InputIterator, typename Predicate>
InputIterator parallel_find_if(InputIterator first, InputIterator last, Predicate predicate)
{
    const std::size_t total = detail::algorithm_range_size(first, last, "parallel_find_if");
    if (total == 0)
        return first;

    using InputValue = typename std::iterator_traits<InputIterator>::value_type;
    const std::size_t callsite =
        detail::algorithm_identity(detail::ParallelAlgorithmKind::FindIf, predicate);
    auto sequential_route = [=]() mutable -> InputIterator
    {
        return std::find_if(first, last, predicate);
    };
    auto scheduled_route = [=](std::size_t scheduler_callsite) mutable -> InputIterator
    {
        const std::size_t best = detail::parallel_find_index(
            detail::ParallelAlgorithmKind::FindIf,
            first,
            total,
            predicate,
            scheduler_callsite);
        return detail::algorithm_advance(first, best);
    };
    return detail::run_hot_dispatched_algorithm(
        detail::ParallelAlgorithmKind::FindIf,
        callsite,
        total,
        sizeof(InputValue),
        sequential_route,
        scheduled_route);
}

template <typename InputIterator, typename T>
InputIterator parallel_find(InputIterator first, InputIterator last, const T& value)
{
    const std::size_t total = detail::algorithm_range_size(first, last, "parallel_find");
    if (total == 0)
        return first;

    using InputValue = typename std::iterator_traits<InputIterator>::value_type;
    using FindValue = std::decay_t<T>;
    static_assert(std::is_copy_constructible_v<FindValue>,
                  "SmartParallel parallel_find requires a copy-constructible value");
    const FindValue find_value(value);
    std::size_t callsite =
        detail::typed_algorithm_identity<FindValue>(detail::ParallelAlgorithmKind::Find);
    callsite = detail::combine_hash(callsite, typeid(InputIterator).hash_code());

    auto sequential_route = [=]() mutable -> InputIterator
    {
        return std::find(first, last, find_value);
    };
    auto scheduled_route = [=](std::size_t scheduler_callsite) mutable -> InputIterator
    {
        const auto predicate = [find_value](const auto& item) { return item == find_value; };
        const std::size_t best = detail::parallel_find_index(
            detail::ParallelAlgorithmKind::Find,
            first,
            total,
            predicate,
            scheduler_callsite);
        return detail::algorithm_advance(first, best);
    };
    return detail::run_hot_dispatched_algorithm(
        detail::ParallelAlgorithmKind::Find,
        callsite,
        total,
        sizeof(InputValue),
        sequential_route,
        scheduled_route);
}
} // namespace smart

#include <smart/numerical/reductions.hpp>
