/// @file api/kmx/aio/quic/byte_buffer.hpp
/// @brief The byte queue behind both directions of a QUIC stream.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_QUIC)
    #ifndef PCH
        #include <cstddef>
        #include <vector>
    #endif

namespace kmx::aio::quic
{
    /// @brief A byte queue that is filled at one end and drained from the other.
    ///
    /// @note Both directions of a stream are pure FIFOs, and the obvious way to write one - append to a
    ///       container, erase what has been taken from its front - moves every byte still queued on every
    ///       read. A protocol that reads a short header before its payload pays that for the whole payload,
    ///       and a stream read in small pieces costs a quadratic in what passes through it.
    ///
    /// @note Instead nothing moves when bytes are taken; a cursor advances. The storage behind the cursor is
    ///       reclaimed only once it accounts for half the buffer, so a compaction never moves more bytes than
    ///       have already been consumed since the last one - which makes its amortised cost a constant per
    ///       byte rather than a factor on the queue's length.
    class byte_buffer
    {
    public:
        /// @brief Whether nothing is queued.
        [[nodiscard]] bool empty() const noexcept { return read_pos_ == data_.size(); }

        /// @brief How many bytes are queued and not yet taken.
        [[nodiscard]] std::size_t size() const noexcept { return data_.size() - read_pos_; }

        /// @brief The queued bytes, contiguously; valid until the next append() or consume().
        [[nodiscard]] const char* data() const noexcept { return data_.data() + read_pos_; }

        /// @brief Queues @p count bytes read from @p first.
        void append(const char* const first, const std::size_t count) noexcept(false) { data_.insert(data_.end(), first, first + count); }

        /// @brief Drops the first @p count queued bytes, which must not exceed size().
        void consume(std::size_t count) noexcept;

    private:
        std::vector<char> data_ {}; ///< Queued bytes, preceded by those already taken.
        std::size_t read_pos_ {};   ///< How much of @ref data_ has been taken.
    };
}

#endif // KMX_AIO_FEATURE_QUIC
