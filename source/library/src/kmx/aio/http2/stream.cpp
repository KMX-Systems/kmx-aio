#include <kmx/aio/http2/stream.hpp>

namespace kmx::aio::http2
{

    /// @brief The state an idle stream moves to when a frame crosses it.
    /// @param type The frame's type.
    /// @param end_stream Whether the frame carried the END_STREAM flag.
    /// @param current The state to stay in when the frame moves nothing.
    /// @param half_closed The half-closed state this direction reaches when the headers ended the stream.
    /// @param reserved The reserved state this direction reaches on a PUSH_PROMISE.
    /// @return The state to move to.
    /// @reference RFC 9113 section 5.1, stream states.
    [[nodiscard]] static constexpr stream_state after_idle(const frame_type type, const bool end_stream,
                                                           const stream_state current, const stream_state half_closed,
                                                           const stream_state reserved) noexcept
    {
        switch (type)
        {
            case frame_type::headers:
                return end_stream ? half_closed : stream_state::open;
            case frame_type::push_promise:
                return reserved;
            default:
                return current;
        }
    }

    /// @brief The state a reserved stream moves to: headers open the promised half, a reset closes it.
    /// @param type The frame's type.
    /// @param current The state to stay in when the frame moves nothing.
    /// @param opened The half-closed state headers move this direction to.
    /// @return The state to move to.
    [[nodiscard]] static constexpr stream_state after_reserved(const frame_type type, const stream_state current,
                                                               const stream_state opened) noexcept
    {
        switch (type)
        {
            case frame_type::headers:
                return opened;
            case frame_type::rst_stream:
                return stream_state::closed;
            default:
                return current;
        }
    }

    /// @brief The state a stream still carrying data moves to.
    /// @param type The frame's type.
    /// @param end_stream Whether the frame carried the END_STREAM flag.
    /// @param current The state to stay in while the stream continues.
    /// @param finished The state reached once this direction is done.
    /// @return The state to move to.
    /// @note A reset closes the stream whatever else the frame said.
    [[nodiscard]] static constexpr stream_state after_data(const frame_type type, const bool end_stream,
                                                           const stream_state current, const stream_state finished) noexcept
    {
        if (type == frame_type::rst_stream)
            return stream_state::closed;
        return end_stream ? finished : current;
    }

    void stream::on_frame_sent(const frame_type type, const bool end_stream) noexcept(false)
    {
        // Read from this endpoint's side: the half a stream closes by sending is the local one.
        switch (state_)
        {
            case stream_state::idle:
                state_ = after_idle(type, end_stream, state_, stream_state::half_closed_local, stream_state::reserved_local);
                break;
            case stream_state::reserved_local:
                state_ = after_reserved(type, state_, stream_state::half_closed_remote);
                break;
            case stream_state::open:
                state_ = after_data(type, end_stream, state_, stream_state::half_closed_local);
                break;
            case stream_state::half_closed_remote:
                state_ = after_data(type, end_stream, state_, stream_state::closed);
                break;
            case stream_state::half_closed_local:
            case stream_state::closed:
            case stream_state::reserved_remote:
                // Nothing but a reset moves a stream this endpoint has already finished sending on.
                state_ = (type == frame_type::rst_stream) ? stream_state::closed : state_;
                break;
        }
    }

    void stream::on_frame_received(const frame_type type, const bool end_stream) noexcept(false)
    {
        // The mirror of on_frame_sent: what the peer sends closes the remote half, not the local one.
        switch (state_)
        {
            case stream_state::idle:
                state_ = after_idle(type, end_stream, state_, stream_state::half_closed_remote, stream_state::reserved_remote);
                break;
            case stream_state::reserved_remote:
                state_ = after_reserved(type, state_, stream_state::half_closed_local);
                break;
            case stream_state::open:
                state_ = after_data(type, end_stream, state_, stream_state::half_closed_remote);
                break;
            case stream_state::half_closed_local:
                state_ = after_data(type, end_stream, state_, stream_state::closed);
                break;
            case stream_state::half_closed_remote:
            case stream_state::closed:
            case stream_state::reserved_local:
                // Nothing but a reset moves a stream this endpoint has already finished receiving on.
                state_ = (type == frame_type::rst_stream) ? stream_state::closed : state_;
                break;
        }
    }

} // namespace kmx::aio::http2
