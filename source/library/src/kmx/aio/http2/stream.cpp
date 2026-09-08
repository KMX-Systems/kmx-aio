#include <kmx/aio/http2/stream.hpp>

namespace kmx::aio::http2
{

    void stream::on_frame_sent(frame_type type, bool end_stream) noexcept(false)
    {
        switch (state_)
        {
            case stream_state::idle:
                switch (type)
                {
                    case frame_type::headers:
                        state_ = end_stream ? stream_state::half_closed_local : stream_state::open;
                        break;
                    case frame_type::push_promise:
                        state_ = stream_state::reserved_local;
                        break;
                    default:
                        break;
                }
                break;

            case stream_state::reserved_local:
                switch (type)
                {
                    case frame_type::headers:
                        state_ = stream_state::half_closed_remote;
                        break;
                    case frame_type::rst_stream:
                        state_ = stream_state::closed;
                        break;
                    default:
                        break;
                }
                break;

            case stream_state::open:
                switch (type)
                {
                    case frame_type::rst_stream:
                        state_ = stream_state::closed;
                        break;
                    default:
                        if (end_stream)
                            state_ = stream_state::half_closed_local;
                        break;
                }
                break;

            case stream_state::half_closed_remote:
                switch (type)
                {
                    case frame_type::rst_stream:
                        state_ = stream_state::closed;
                        break;
                    default:
                        if (end_stream)
                            state_ = stream_state::closed;
                        break;
                }
                break;

            case stream_state::half_closed_local:
            case stream_state::closed:
            case stream_state::reserved_remote:
                switch (type)
                {
                    case frame_type::rst_stream:
                        state_ = stream_state::closed;
                        break;
                    default:
                        break;
                }
                break;
        }
    }

    void stream::on_frame_received(frame_type type, bool end_stream) noexcept(false)
    {
        switch (state_)
        {
            case stream_state::idle:
                switch (type)
                {
                    case frame_type::headers:
                        state_ = end_stream ? stream_state::half_closed_remote : stream_state::open;
                        break;
                    case frame_type::push_promise:
                        state_ = stream_state::reserved_remote;
                        break;
                    default:
                        break;
                }
                break;

            case stream_state::reserved_remote:
                switch (type)
                {
                    case frame_type::headers:
                        state_ = stream_state::half_closed_local;
                        break;
                    case frame_type::rst_stream:
                        state_ = stream_state::closed;
                        break;
                    default:
                        break;
                }
                break;

            case stream_state::open:
                switch (type)
                {
                    case frame_type::rst_stream:
                        state_ = stream_state::closed;
                        break;
                    default:
                        if (end_stream)
                            state_ = stream_state::half_closed_remote;
                        break;
                }
                break;

            case stream_state::half_closed_local:
                switch (type)
                {
                    case frame_type::rst_stream:
                        state_ = stream_state::closed;
                        break;
                    default:
                        if (end_stream)
                            state_ = stream_state::closed;
                        break;
                }
                break;

            case stream_state::half_closed_remote:
            case stream_state::closed:
            case stream_state::reserved_local:
                switch (type)
                {
                    case frame_type::rst_stream:
                        state_ = stream_state::closed;
                        break;
                    default:
                        break;
                }
                break;
        }
    }

} // namespace kmx::aio::http2
