#pragma once

#include <smart/core/safe_arithmetic.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace smart::data
{
enum class MemoryDomain
{
    Host
};

enum class OverlapKind
{
    ExactMapping,
    Disjoint,
    Overlap,
    Unknown
};

template <typename T, std::size_t Rank>
class View
{
    static_assert(Rank > 0, "SmartParallel View rank must be positive");

  public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using pointer = T*;
    using reference = T&;
    using Extents = std::array<std::size_t, Rank>;
    using Strides = std::array<std::size_t, Rank>;

    View() = default;

    View(pointer data,
         Extents extents,
         Strides strides,
         std::size_t declared_alignment = 0)
        : data_(data), extents_(extents), strides_(strides),
          declared_alignment_(declared_alignment)
    {
        validate();
    }

    template <typename U,
              typename = std::enable_if_t<std::is_const_v<T>
                                          && std::is_same_v<std::remove_const_t<T>, U>>>
    View(const View<U, Rank>& other) noexcept
        : data_(other.data()), extents_(other.extents()), strides_(other.strides()),
          declared_alignment_(other.declared_alignment()), logical_size_(other.size())
    {
    }

    static View contiguous(pointer data,
                           Extents extents,
                           std::size_t declared_alignment = 0)
    {
        Strides strides{};
        bool has_zero_extent = false;
        for (const std::size_t extent : extents)
            has_zero_extent = has_zero_extent || extent == 0;
        strides[Rank - 1] = 1;
        for (std::size_t dimension = Rank - 1; dimension > 0; --dimension)
        {
            const auto product = saturating_multiply(strides[dimension], extents[dimension]);
            if (product.saturated)
            {
                if (!has_zero_extent)
                    throw std::overflow_error("SmartParallel View contiguous stride overflow");
                strides[dimension - 1] = 0;
            }
            else
            {
                strides[dimension - 1] = product.value;
            }
        }
        return View(data, extents, strides, declared_alignment);
    }

    pointer data() const noexcept { return data_; }
    const Extents& extents() const noexcept { return extents_; }
    const Strides& strides() const noexcept { return strides_; }
    std::size_t extent(std::size_t dimension) const
    {
        if (dimension >= Rank)
            throw std::out_of_range("SmartParallel View dimension out of range");
        return extents_[dimension];
    }
    std::size_t stride(std::size_t dimension) const
    {
        if (dimension >= Rank)
            throw std::out_of_range("SmartParallel View dimension out of range");
        return strides_[dimension];
    }
    std::size_t declared_alignment() const noexcept { return declared_alignment_; }
    MemoryDomain memory_domain() const noexcept { return MemoryDomain::Host; }
    static constexpr std::size_t rank() noexcept { return Rank; }
    bool empty() const noexcept { return logical_size_ == 0; }
    std::size_t size() const noexcept { return logical_size_; }

    bool is_contiguous() const noexcept
    {
        if (empty())
            return true;
        std::size_t expected = 1;
        for (std::size_t dimension = Rank; dimension-- > 0;)
        {
            if (extents_[dimension] > 1 && strides_[dimension] != expected)
                return false;
            const auto product = saturating_multiply(expected, extents_[dimension]);
            if (product.saturated)
                return false;
            expected = product.value;
        }
        return true;
    }

    bool has_unique_mapping() const noexcept
    {
        if (empty() || logical_size_ == 1)
            return true;
        if constexpr (Rank == 1)
        {
            return extents_[0] <= 1 || strides_[0] != 0;
        }
        else if constexpr (Rank == 2)
        {
            const std::size_t rows = extents_[0];
            const std::size_t columns = extents_[1];
            const std::size_t row_stride = strides_[0];
            const std::size_t column_stride = strides_[1];
            if (rows <= 1)
                return columns <= 1 || column_stride != 0;
            if (columns <= 1)
                return row_stride != 0;
            if (row_stride == 0 || column_stride == 0)
                return false;
            const std::size_t divisor = std::gcd(row_stride, column_stride);
            const std::size_t row_difference = column_stride / divisor;
            const std::size_t column_difference = row_stride / divisor;
            return !(row_difference < rows && column_difference < columns);
        }
        else
        {
            return is_contiguous();
        }
    }

    template <typename... Indices>
    reference operator()(Indices... indices) const
    {
        static_assert(sizeof...(Indices) == Rank,
                      "SmartParallel View indexing requires exactly Rank indices");
        const std::array<std::size_t, Rank> index{static_cast<std::size_t>(indices)...};
        std::size_t offset = 0;
        for (std::size_t dimension = 0; dimension < Rank; ++dimension)
        {
            if (index[dimension] >= extents_[dimension])
                throw std::out_of_range("SmartParallel View index out of range");
            const auto term = saturating_multiply(index[dimension], strides_[dimension]);
            const auto sum = saturating_add(offset, term.value);
            if (term.saturated || sum.saturated)
                throw std::overflow_error("SmartParallel View address offset overflow");
            offset = sum.value;
        }
        return data_[offset];
    }

    bool same_mapping(const View& other) const noexcept
    {
        return data_ == other.data_ && extents_ == other.extents_ && strides_ == other.strides_;
    }

    template <typename U>
    OverlapKind overlap(const View<U, Rank>& other) const noexcept
    {
        if (empty() || other.empty())
            return OverlapKind::Disjoint;
        if (reinterpret_cast<const void*>(data_) == reinterpret_cast<const void*>(other.data())
            && extents_ == other.extents() && strides_ == other.strides())
            return OverlapKind::ExactMapping;
        const auto left_begin = reinterpret_cast<std::uintptr_t>(data_);
        const auto right_begin = reinterpret_cast<std::uintptr_t>(other.data());
        std::size_t left_max_offset = 0;
        std::size_t right_max_offset = 0;
        for (std::size_t dimension = 0; dimension < Rank; ++dimension)
        {
            const auto left_term = saturating_multiply(extents_[dimension] - 1, strides_[dimension]);
            const auto left_sum = saturating_add(left_max_offset, left_term.value);
            const auto right_term = saturating_multiply(other.extent(dimension) - 1,
                                                        other.stride(dimension));
            const auto right_sum = saturating_add(right_max_offset, right_term.value);
            if (left_term.saturated || left_sum.saturated
                || right_term.saturated || right_sum.saturated)
                return OverlapKind::Unknown;
            left_max_offset = left_sum.value;
            right_max_offset = right_sum.value;
        }
        const auto left_elements = saturating_add(left_max_offset, std::size_t{1});
        const auto right_elements = saturating_add(right_max_offset, std::size_t{1});
        if (left_elements.saturated || right_elements.saturated)
            return OverlapKind::Unknown;
        const auto left_bytes = saturating_multiply(left_elements.value, sizeof(value_type));
        const auto right_bytes = saturating_multiply(
            right_elements.value, sizeof(typename View<U, Rank>::value_type));
        if (left_bytes.saturated || right_bytes.saturated)
            return OverlapKind::Unknown;
        if (left_begin > std::numeric_limits<std::uintptr_t>::max() - left_bytes.value
            || right_begin > std::numeric_limits<std::uintptr_t>::max() - right_bytes.value)
            return OverlapKind::Unknown;
        const auto left_end = left_begin + left_bytes.value;
        const auto right_end = right_begin + right_bytes.value;
        return left_end <= right_begin || right_end <= left_begin
            ? OverlapKind::Disjoint
            : OverlapKind::Overlap;
    }

  private:
    void validate()
    {
        if (declared_alignment_ != 0)
        {
            if ((declared_alignment_ & (declared_alignment_ - 1)) != 0)
                throw std::invalid_argument("SmartParallel View alignment must be a power of two");
            if (data_ != nullptr
                && reinterpret_cast<std::uintptr_t>(data_) % declared_alignment_ != 0)
                throw std::invalid_argument("SmartParallel View pointer does not satisfy declared alignment");
        }

        for (const std::size_t extent : extents_)
        {
            if (extent == 0)
            {
                logical_size_ = 0;
                return;
            }
        }

        logical_size_ = 1;
        for (std::size_t dimension = 0; dimension < Rank; ++dimension)
        {
            const auto product = saturating_multiply(logical_size_, extents_[dimension]);
            if (product.saturated)
                throw std::overflow_error("SmartParallel View logical size overflow");
            logical_size_ = product.value;
        }
        if (logical_size_ != 0 && data_ == nullptr)
            throw std::invalid_argument("SmartParallel non-empty View requires non-null data");
        std::size_t maximum_offset = 0;
        for (std::size_t dimension = 0; dimension < Rank; ++dimension)
        {
            const auto term = saturating_multiply(extents_[dimension] - 1, strides_[dimension]);
            const auto sum = saturating_add(maximum_offset, term.value);
            if (term.saturated || sum.saturated)
                throw std::overflow_error("SmartParallel View address span overflow");
            maximum_offset = sum.value;
        }
        const auto span_elements = saturating_add(maximum_offset, std::size_t{1});
        if (span_elements.saturated)
            throw std::overflow_error("SmartParallel View address span overflow");
        const auto bytes = saturating_multiply(span_elements.value, sizeof(value_type));
        if (bytes.saturated)
            throw std::overflow_error("SmartParallel View byte span overflow");
        const auto address = reinterpret_cast<std::uintptr_t>(data_);
        if (address > std::numeric_limits<std::uintptr_t>::max() - bytes.value)
            throw std::overflow_error("SmartParallel View address range overflow");
    }

    pointer data_ = nullptr;
    Extents extents_{};
    Strides strides_{};
    std::size_t declared_alignment_ = 0;
    std::size_t logical_size_ = 0;
};

template <typename T>
using VectorView = View<T, 1>;

template <typename T>
using MatrixView = View<T, 2>;
} // namespace smart::data
