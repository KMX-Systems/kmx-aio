#include <kmx/aio/http3/stream.hpp>

#include <stdexcept>

namespace kmx::aio::http3
{
    [[nodiscard]] static constexpr bool is_request_stream_frame(const frame_type type) noexcept
    {
        return type == frame_type::headers || type == frame_type::data;
    }

    void stream::on_frame(const frame_type type, const stream_state half_closed_state,
                          const stream_frame_messages& messages) noexcept(false)
    {
        if (!is_request_stream_frame(type))
            throw std::invalid_argument(messages.unsupported_frame);

        if (!headers_seen_)
        {
            if (type != frame_type::headers)
                throw std::logic_error(messages.missing_headers);
            headers_seen_ = true;
            state_ = stream_state::open;
            return;
        }

        if ((state_ == stream_state::closed) || (state_ == half_closed_state))
            throw std::logic_error(messages.closed_side);
    }

    void stream::on_frame_sent(const frame_type type) noexcept(false)
    {
        static constexpr stream_frame_messages messages {.unsupported_frame = "HTTP/3 stream received unsupported outgoing frame type",
                                                         .missing_headers = "HTTP/3 request stream must start with HEADERS",
                                                         .closed_side = "cannot send frame on closed local HTTP/3 stream side"};

        on_frame(type, stream_state::half_closed_local, messages);
    }

    void stream::on_frame_received(const frame_type type) noexcept(false)
    {
        static constexpr stream_frame_messages messages {.unsupported_frame = "HTTP/3 stream received unsupported incoming frame type",
                                                         .missing_headers = "HTTP/3 response stream must start with HEADERS",
                                                         .closed_side = "cannot receive frame on closed remote HTTP/3 stream side"};

        on_frame(type, stream_state::half_closed_remote, messages);
    }

    void stream::on_send_fin() noexcept
    {
        send_closed_ = true;
        state_ = recv_closed_ ? stream_state::closed : stream_state::half_closed_local;
    }

    void stream::on_recv_fin() noexcept
    {
        recv_closed_ = true;
        state_ = send_closed_ ? stream_state::closed : stream_state::half_closed_remote;
    }

    void stream::on_reset() noexcept
    {
        send_closed_ = true;
        recv_closed_ = true;
        state_ = stream_state::closed;
    }
} // namespace kmx::aio::http3