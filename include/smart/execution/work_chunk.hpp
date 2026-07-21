#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <smart/execution/execution_context.hpp>

namespace smart
{
struct WorkChunk
{
    std::size_t begin = 0;
    std::size_t end = 0;
    std::size_t ordinal = 0;

    bool valid() const noexcept
    {
        return begin < end;
    }

    std::size_t size() const noexcept
    {
        return valid() ? end - begin : 0;
    }
};

struct WorkChunkProgress
{
    std::size_t total_iterations = 0;
    std::size_t chunk_size = 1;
    std::size_t total_chunks = 0;
    std::size_t acquired_chunks = 0;
    std::size_t completed_chunks = 0;
    std::size_t acquired_iterations = 0;
    std::size_t completed_iterations = 0;

    bool fully_acquired() const noexcept
    {
        return acquired_iterations >= total_iterations;
    }

    bool complete() const noexcept
    {
        return completed_iterations >= total_iterations;
    }
};

// A small scheduler-facing representation of the unstarted and completed work
// belonging to one loop region. Acquisition is lock-free and deterministic with
// respect to chunk ordinals: every successful caller receives the next ordinal.
class SchedulerVisibleWork
{
  public:
    SchedulerVisibleWork(std::size_t begin,
                         std::size_t end,
                         std::size_t chunk_size,
                         const ExecutionContext& owner = current_execution_context()) noexcept
        : begin_(begin),
          end_(std::max(begin, end)),
          chunk_size_(std::max<std::size_t>(1, chunk_size)),
          owner_loop_id_(owner.loop_id),
          parent_loop_id_(owner.parent_loop_id),
          owner_depth_(owner.depth)
    {
    }

    WorkChunk try_acquire() noexcept
    {
        if (cancelled())
            return {};
        const std::size_t offset = next_offset_.fetch_add(chunk_size_, std::memory_order_relaxed);
        const std::size_t total = total_iterations();
        if (offset >= total)
            return {};

        const std::size_t chunk_begin = begin_ + offset;
        const std::size_t chunk_end = begin_ + std::min(total, offset + chunk_size_);
        const std::size_t ordinal = offset / chunk_size_;
        acquired_chunks_.fetch_add(1, std::memory_order_relaxed);
        acquired_iterations_.fetch_add(chunk_end - chunk_begin, std::memory_order_relaxed);
        return WorkChunk{chunk_begin, chunk_end, ordinal};
    }

    void mark_complete(const WorkChunk& chunk) noexcept
    {
        if (!chunk.valid())
            return;
        completed_chunks_.fetch_add(1, std::memory_order_relaxed);
        completed_iterations_.fetch_add(chunk.size(), std::memory_order_relaxed);
    }

    std::size_t total_iterations() const noexcept
    {
        return end_ - begin_;
    }

    std::size_t total_chunks() const noexcept
    {
        const std::size_t total = total_iterations();
        return total == 0 ? 0 : (total + chunk_size_ - 1) / chunk_size_;
    }

    std::uint64_t owner_loop_id() const noexcept { return owner_loop_id_; }
    std::uint64_t parent_loop_id() const noexcept { return parent_loop_id_; }
    std::size_t owner_depth() const noexcept { return owner_depth_; }
    std::size_t chunk_size() const noexcept { return chunk_size_; }


    bool cancelled() const noexcept
    {
        return cancelled_.load(std::memory_order_acquire);
    }

    void request_cancel() noexcept
    {
        cancelled_.store(true, std::memory_order_release);
    }

    void capture_exception(std::exception_ptr error) noexcept
    {
        if (!error)
            return;
        {
            std::lock_guard<std::mutex> lock(exception_mutex_);
            if (!first_exception_)
                first_exception_ = error;
        }
        request_cancel();
    }

    bool has_exception() const noexcept
    {
        std::lock_guard<std::mutex> lock(exception_mutex_);
        return static_cast<bool>(first_exception_);
    }

    void rethrow_if_failed() const
    {
        std::exception_ptr error;
        {
            std::lock_guard<std::mutex> lock(exception_mutex_);
            error = first_exception_;
        }
        if (error)
            std::rethrow_exception(error);
    }

    WorkChunkProgress progress() const noexcept
    {
        WorkChunkProgress result;
        result.total_iterations = total_iterations();
        result.chunk_size = chunk_size_;
        result.total_chunks = total_chunks();
        result.acquired_chunks = acquired_chunks_.load(std::memory_order_relaxed);
        result.completed_chunks = completed_chunks_.load(std::memory_order_relaxed);
        result.acquired_iterations = acquired_iterations_.load(std::memory_order_relaxed);
        result.completed_iterations = completed_iterations_.load(std::memory_order_relaxed);
        return result;
    }

  private:
    std::size_t begin_ = 0;
    std::size_t end_ = 0;
    std::size_t chunk_size_ = 1;
    std::uint64_t owner_loop_id_ = 0;
    std::uint64_t parent_loop_id_ = 0;
    std::size_t owner_depth_ = 0;
    std::atomic<std::size_t> next_offset_{0};
    std::atomic<std::size_t> acquired_chunks_{0};
    std::atomic<std::size_t> completed_chunks_{0};
    std::atomic<std::size_t> acquired_iterations_{0};
    std::atomic<std::size_t> completed_iterations_{0};
    std::atomic<bool> cancelled_{false};
    mutable std::mutex exception_mutex_;
    std::exception_ptr first_exception_;
};
} // namespace smart
