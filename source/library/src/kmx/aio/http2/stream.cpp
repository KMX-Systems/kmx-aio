#include <kmx/aio/http2/stream.hpp>

namespace kmx::aio::http2
{

    void stream::on_frame_sent(frame_type type, bool end_stream) noexcept(false)
    {
        switch (state_)
        {
            case stream_state::idle:
                if (type == frame_type::headers)
                {
                    state_ = end_stream ? stream_state::half_closed_local : stream_state::open;
                }
                else if (type == frame_type::push_promise)
                {
                    state_ = stream_state::reserved_local;
                }
                break;

            case stream_state::reserved_local:
                if (type == frame_type::headers)
                {
                    state_ = stream_state::half_closed_remote;
                }
                else if (type == frame_type::rst_stream)
                {
                    state_ = stream_state::closed;
                }
                break;

            case stream_state::open:
                if (end_stream)
                {
                    state_ = stream_state::half_closed_local;
                }
                else if (type == frame_type::rst_stream)
                {
                    state_ = stream_state::closed;
                }
                break;

            case stream_state::half_closed_remote:
                if (end_stream)
                {
                    state_ = stream_state::closed;
                }
                else if (type == frame_type::rst_stream)
                {
                    state_ = stream_state::closed;
                }
                break;

            case stream_state::half_closed_local:
            case stream_state::closed:
            case stream_state::reserved_remote:
                if (type == frame_type::rst_stream)
                {
                    state_ = stream_state::closed;
                }
                break;
        }
    }

    void stream::on_frame_received(frame_type type, bool end_stream) noexcept(false)
    {
        switch (state_)
        {
            case stream_state::idle:
                if (type == frame_type::headers)
                {
                    state_ = end_stream ? stream_state::half_closed_remote : stream_state::open;
                }
                else if (type == frame_type::push_promise)
                {
                    state_ = stream_state::reserved_remote;
                }
                break;

            case stream_state::reserved_remote:
                if (type == frame_type::headers)
                {
                    state_ = stream_state::half_closed_local;
                }
                else if (type == frame_type::rst_stream)
                {
                    state_ = stream_state::closed;
                }
                break;

            case stream_state::open:
                if (end_stream)
                {
                    state_ = stream_state::half_closed_remote;
                }
                else if (type == frame_type::rst_stream)
                {
                    state_ = stream_state::closed;
                }
                break;

            case stream_state::half_closed_local:
                if (end_stream)
                {
                    state_ = stream_state::closed;
                }
                else if (type == frame_type::rst_stream)
                {
                    state_ = stream_state::closed;
                }
                break;

            case stream_state::half_closed_remote:
            case stream_state::closed:
            case stream_state::reserved_local:
                if (type == frame_type::rst_stream)
                {
                    state_ = stream_state::closed;
                }
                break;
        }
    }

} // namespace kmx::aio::http2
