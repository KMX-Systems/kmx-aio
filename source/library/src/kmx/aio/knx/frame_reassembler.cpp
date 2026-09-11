/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/detail/frame_reassembler.hpp>

#include <kmx/aio/knx/error.hpp>

#include <algorithm>
#include <cstring>
#include <utility>

namespace kmx::aio::knx::detail
{
    /// @brief The only KNXnet/IP protocol version.
    static constexpr std::uint8_t protocol_version = 0x10u;

    void frame_reassembler::discard_taken() noexcept
    {
        begin_ += std::exchange(taken_, 0u);
        if (begin_ == end_)
        {
            begin_ = 0u;
            end_ = 0u;
        }
    }

    span_uint8_t frame_reassembler::writable() noexcept
    {
        discard_taken();
        // The unfinished frame moves to the front, so that a frame of the full capacity always fits behind its start.
        if (begin_ != 0u)
        {
            std::memmove(buffer_.data(), buffer_.data() + begin_, end_ - begin_);
            end_ -= begin_;
            begin_ = 0u;
        }
        return span_uint8_t {buffer_}.subspan(end_);
    }

    void frame_reassembler::commit(const std::size_t count) noexcept
    {
        end_ += std::min(count, capacity - end_);
    }

    reassembled_frame_result_t frame_reassembler::next() noexcept
    {
        discard_taken();
        const auto available = end_ - begin_;
        if (available < frame::communication_header_size)
            return std::optional<cspan_uint8_t> {};

        // Checked the moment a header is complete, before a single further octet of its frame is waited for.
        const auto* const header = buffer_.data() + begin_;
        if ((header[0u] != frame::communication_header_size) || (header[1u] != protocol_version))
            return std::unexpected(make_error_code(error::malformed_frame));
        const auto total = (std::size_t {header[4u]} << 8u) | std::size_t {header[5u]};
        if (total < frame::communication_header_size)
            return std::unexpected(make_error_code(error::malformed_frame));
        if (total > capacity)
            return std::unexpected(make_error_code(error::invalid_length));
        if (available < total)
            return std::optional<cspan_uint8_t> {};

        taken_ = total;
        return std::optional<cspan_uint8_t> {cspan_uint8_t {header, total}};
    }

    void frame_reassembler::reset() noexcept
    {
        begin_ = 0u;
        end_ = 0u;
        taken_ = 0u;
    }
}
