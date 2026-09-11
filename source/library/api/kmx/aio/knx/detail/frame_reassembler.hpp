/// @file kmx/aio/knx/detail/frame_reassembler.hpp
/// @brief Cuts a byte stream into whole KNXnet/IP frames, which KNXnet/IP over TCP needs and UDP never did.
/// @details
/// Over UDP a datagram is a frame. Over TCP a read returns whatever the stream holds - part of a header, a frame and
/// a half, three frames at once - so frames have to be recovered from the total length in each KNXnet/IP header.
/// This reassembler buffers what has arrived, checks a header as soon as its six octets are in - the header length,
/// the protocol version, and a total length no shorter than the header and no longer than the limit - and hands out
/// frames whole. What arrived of an unfinished frame survives between calls, so a receive whose deadline expires in
/// the middle of a frame loses nothing: the rest completes the frame on the next receive.
///
/// It owns no socket and does no I/O. A transport asks @ref kmx::aio::knx::detail::frame_reassembler::next for a
/// frame, reads into @ref kmx::aio::knx::detail::frame_reassembler::writable when there is none, and reports how many
/// octets it read with @ref kmx::aio::knx::detail::frame_reassembler::commit.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <array>
        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <optional>
        #include <system_error>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/frame.hpp>

namespace kmx::aio::knx::detail
{
    /// @brief A whole frame, nothing while one is still arriving, or why the stream cannot be read any further.
    using reassembled_frame_result_t = std::expected<std::optional<cspan_uint8_t>, std::error_code>;

    /// @brief Recovers whole KNXnet/IP frames from a byte stream.
    /// @note Not thread-safe: one receive path owns it.
    class frame_reassembler final
    {
    public:
        /// @brief The longest frame accepted, which is also the buffer's size.
        static constexpr std::size_t capacity = frame::max_datagram_size;

        /// @brief Returns where the next read should put its octets.
        /// @return The free space behind what is held; never empty while no whole frame is waiting.
        /// @note Call @ref next first: a whole frame still held is not moved out of the way.
        [[nodiscard]] span_uint8_t writable() noexcept;

        /// @brief Records that @p count octets were read into what @ref writable returned.
        /// @param count The octets read; anything beyond the space @ref writable offered is ignored.
        void commit(std::size_t count) noexcept;

        /// @brief Takes the next whole frame.
        /// @return The frame, valid until the next call on this object, or nothing while it is incomplete.
        /// @retval kmx::aio::knx::error::malformed_frame The header length or protocol version is wrong, or the total
        ///         length is shorter than a header. The stream cannot be resynchronised; @ref reset it.
        /// @retval kmx::aio::knx::error::invalid_length The total length exceeds @ref capacity.
        [[nodiscard]] reassembled_frame_result_t next() noexcept;

        /// @brief Indicates whether octets of an unfinished frame are held.
        /// @note Consulted when the stream ends: an end in the middle of a frame is a truncated frame, not a close.
        [[nodiscard]] bool partial() const noexcept { return (end_ - begin_) > taken_; }

        /// @brief Drops everything held, as after a reconnect or once the stream is known to be broken.
        void reset() noexcept;

    private:
        /// @brief Drops the frame @ref next handed out last.
        void discard_taken() noexcept;

        std::array<std::uint8_t, capacity> buffer_ {};
        /// @brief The first octet not yet handed out.
        std::size_t begin_ {};
        /// @brief One past the last octet held.
        std::size_t end_ {};
        /// @brief The length of the frame @ref next handed out, dropped on the next call.
        std::size_t taken_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
