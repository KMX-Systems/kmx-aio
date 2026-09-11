/// @file api/kmx/aio/quic/transport.hpp
/// @brief The state a QUIC byte stream shares with its endpoint, its read high-water mark, and server ALPN selection.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// @note This is a separate layer from aio/quic/generic_engine.hpp rather than a change to it. That engine delivers
///       received bytes by spawning a detached task per 4 KiB chunk, which means chunks of one stream can be
///       in flight concurrently and complete out of order - acceptable for a fire-and-forget echo sample,
///       not for anything that has to parse a byte stream. It also has no way to open a stream on demand, and
///       no way for a server to initiate one. Rather than change behaviour the existing samples depend on,
///       this provides what a protocol layer actually needs: a stream you can read from and write to, in
///       order, with backpressure, and which suspends rather than drops.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_QUIC)
    #ifndef PCH
        #include <kmx/aio/promise_base.hpp>
        #include <kmx/aio/quic/byte_buffer.hpp>

        #include <lsquic.h>

        #include <cstddef>
        #include <system_error>
    #endif

namespace kmx::aio::quic
{
    /// @brief Everything one QUIC stream needs to behave like a byte stream.
    /// @note Held by shared_ptr because lsquic can close a stream at any point, while a coroutine may still be
    ///       suspended on it. The callback drops its reference and the awaiting side finds the stream finished
    ///       rather than a dangling pointer.
    struct stream_state
    {
        ::lsquic_stream_t* handle {}; ///< The lsquic stream, or null once it has closed.
        ::lsquic_conn_t* conn {};     ///< The connection it belongs to; kept after @ref handle is cleared.
        byte_buffer incoming {};      ///< Bytes received and not yet read.
        byte_buffer outgoing {};      ///< Bytes queued for writing, not yet accepted by lsquic.
        coroutine_handle_t reader {}; ///< Suspended reader, if any.
        coroutine_handle_t writer {}; ///< Suspended writer, if any.
        bool fin_received {};         ///< The peer finished its direction.
        bool closed {};               ///< The stream is gone.
        std::error_code error {};     ///< Why it ended, if abnormally.
    };

    /// @brief Bytes buffered for one stream before reading is paused.
    /// @note Backpressure rather than a drop. Discarding bytes already read would be a protocol violation the
    ///       peer has no way to detect on a reliable stream - it believes they arrived. Pausing with
    ///       lsquic_stream_wantread() lets QUIC's own flow control do what it is for: stop the sender. The
    ///       engine in primary_base_impl.cpp parks its streams the same way when its payload pool runs dry; the two
    ///       differ only in what they count, bytes buffered here and buffers on loan there.
    constexpr std::size_t stream_read_high_water = 256u * 1024u;

    /// @brief Teaches a server SSL_CTX to accept @p alpn.
    /// @param ssl_ctx The server context, as a void* so this header does not force BoringSSL on every consumer.
    /// @param alpn The protocol name the peer will offer.
    /// @note Needed on the server side only, and easy to miss: ea_alpn makes the *client* offer a name, but
    ///       selecting from the offer is BoringSSL's job and defaults to selecting nothing. The handshake then
    ///       fails with "no suitable application protocol" and no packet ever reaches the application, which
    ///       looks exactly like a connection that hangs.
    void configure_server_alpn(void* ssl_ctx, const char* alpn) noexcept;
}

#endif // KMX_AIO_FEATURE_QUIC
