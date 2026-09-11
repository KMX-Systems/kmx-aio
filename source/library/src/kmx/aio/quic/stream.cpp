/// @file src/kmx/aio/quic/stream.cpp
/// @brief Read, write and shutdown bodies of a QUIC byte stream.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#if defined(KMX_AIO_FEATURE_QUIC)
    #include <kmx/aio/quic/stream.hpp>
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/quic/transport.hpp>

        #include <lsquic.h>

        #include <algorithm>
        #include <cstddef>
        #include <cstdint>
        #include <cstring>
        #include <expected>
        #include <span>
        #include <system_error>
    #endif

namespace kmx::aio::quic
{
    std::uint64_t stream::id() const noexcept
    {
        return (state_ && state_->handle) ? static_cast<std::uint64_t>(::lsquic_stream_id(state_->handle)) : 0u;
    }

    void stream::shutdown_write() noexcept
    {
        if (state_->handle)
            ::lsquic_stream_shutdown(state_->handle, 1);
    }

    task_returning_expected_size_t stream::read(const std::span<char> out) noexcept(false)
    {
        co_await readable {*state_};

        if (state_->error)
            co_return std::unexpected(state_->error);

        if (state_->incoming.empty())
            co_return std::size_t {0u}; // finished, or closed with nothing pending

        const auto count = std::min(out.size(), state_->incoming.size());
        if (count != 0u)
        {
            std::memcpy(out.data(), state_->incoming.data(), count);
            state_->incoming.consume(count);
        }

        // Room again, so let lsquic resume delivering.
        if (state_->handle && (state_->incoming.size() < stream_read_high_water))
            ::lsquic_stream_wantread(state_->handle, 1);

        co_return count;
    }

    task_returning_expected_void_t stream::write_all(const cspan_char_t in) noexcept(false)
    {
        if (state_->closed)
            co_return std::unexpected(state_->error ? state_->error : std::make_error_code(std::errc::broken_pipe));

        state_->outgoing.append(in.data(), in.size());
        if (state_->handle)
            ::lsquic_stream_wantwrite(state_->handle, 1);

        co_await flushed {*state_};

        if (state_->error)
            co_return std::unexpected(state_->error);

        if (!state_->outgoing.empty())
            co_return std::unexpected(std::make_error_code(std::errc::broken_pipe));

        co_return expected_void_t {};
    }
}

#endif // KMX_AIO_FEATURE_QUIC
