#include <kmx/aio/http3/stream.hpp>

#include <stdexcept>

namespace kmx::aio::http3
{
    namespace detail
    {
        [[nodiscard]] bool is_request_stream_frame(const frame_type type) noexcept
        {
            return type == frame_type::headers || type == frame_type::data;
        }
    } // namespace detail

    void stream::on_frame_sent(const frame_type type) noexcept(false)
    {
        if (!detail::is_request_stream_frame(type))
            throw std::invalid_argument("HTTP/3 stream received unsupported outgoing frame type");

        if (!headers_seen_)
        {
            if (type != frame_type::headers)
                throw std::logic_error("HTTP/3 request stream must start with HEADERS");
            headers_seen_ = true;
            state_ = stream_state::open;
            return;
        }

        if (state_ == stream_state::closed || state_ == stream_state::half_closed_local)
            throw std::logic_error("cannot send frame on closed local HTTP/3 stream side");
    }

    void stream::on_frame_received(const frame_type type) noexcept(false)
    {
        if (!detail::is_request_stream_frame(type))
            throw std::invalid_argument("HTTP/3 stream received unsupported incoming frame type");

        if (!headers_seen_)
        {
            if (type != frame_type::headers)
                throw std::logic_error("HTTP/3 response stream must start with HEADERS");
            headers_seen_ = true;
            state_ = stream_state::open;
            return;
        }

        if (state_ == stream_state::closed || state_ == stream_state::half_closed_remote)
            throw std::logic_error("cannot receive frame on closed remote HTTP/3 stream side");
    }

    void stream::on_send_fin() noexcept
    {
        send_closed_ = true;
        if (recv_closed_)
            state_ = stream_state::closed;
        else
            state_ = stream_state::half_closed_local;
    }

    void stream::on_recv_fin() noexcept
    {
        recv_closed_ = true;
        if (send_closed_)
            state_ = stream_state::closed;
        else
            state_ = stream_state::half_closed_remote;
    }

    void stream::on_reset() noexcept
    {
        send_closed_ = true;
        recv_closed_ = true;
        state_ = stream_state::closed;
    }
} // namespace kmx::aio::http3