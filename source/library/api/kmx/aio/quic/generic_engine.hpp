/// @file api/kmx/aio/quic/generic_engine.hpp
/// @brief Generic QUIC engine template consolidated for all I/O models.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_QUIC)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/quic/engine.hpp>
        #include <kmx/aio/quic/settings.hpp>
        #include <kmx/aio/quic/stream_payload.hpp>
        #include <kmx/aio/task.hpp>

        #include <cstddef>
        #include <functional>
        #include <memory>
        #include <string>
    #endif

struct lsquic_stream;
struct lsquic_conn;

namespace kmx::aio::quic
{
    /// @brief Generic QUIC engine template.
    /// @details Provides a unified interface for lsquic-based engines,
    ///          parameterized by Executor and UdpSocket types.
    /// @tparam Executor  The model-specific executor (readiness/completion).
    /// @tparam UdpSocket The model-specific UDP socket.
    template <typename Executor, typename UdpSocket>
    class generic_engine
    {
    public:
        /// @brief Callback invoked when a new QUIC stream is accepted.
        /// @details The stream pointer is non-owning and valid only while the stream remains open.
        using stream_handler_t = std::function<task<void>(::lsquic_stream*, stream_payload)>;
        using post_handshake_stream_writer_t = std::function<void(::lsquic_stream*)>;

        /// @brief Constructor.
        /// @param exec The executor to bind this engine to.
        explicit generic_engine(Executor& exec) noexcept;

        /// @brief Sets the callback for accepted streams.
        void set_stream_handler(stream_handler_t handler) noexcept;

        /// @brief Sets the ALPN identifier passed into lsquic engine setup.
        void set_alpn(std::string alpn) noexcept;

        /// @brief Configures how many local streams to request once handshake completes.
        /// @details This is useful for protocol bootstrap writes (e.g. HTTP/3 control preface).
        void set_post_handshake_stream_count(std::size_t count) noexcept;

        /// @brief Sets the writer callback used for post-handshake bootstrap streams.
        /// @details The callback is called once per bootstrap stream on first write opportunity.
        void set_post_handshake_stream_writer(post_handshake_stream_writer_t writer) noexcept;

        /// @brief Non-copyable.
        generic_engine(const generic_engine&) = delete;
        /// @brief Non-copyable.
        generic_engine& operator=(const generic_engine&) = delete;

        /// @brief Move constructor.
        generic_engine(generic_engine&&) noexcept = default;
        /// @brief Move assignment is disabled.
        generic_engine& operator=(generic_engine&&) noexcept = delete;

        /// @brief Destructor.
        ~generic_engine() noexcept;

        /// @brief Starts the QUIC engine, binding to the specified address.
        /// @param ip      IP address to bind to.
        /// @param port    Port number to bind to.
        /// @param ssl_ctx BoringSSL SSL_CTX pointer.
        /// @param config  QUIC protocol settings.
        /// @return Success or an error code.
        [[nodiscard]] task_returning_expected_void_t start(ip_address_t ip, port_t port, void* ssl_ctx = nullptr,
                                                           const settings& config = settings {}) noexcept(false);

        /// @brief Processes pending QUIC events (called from the event loop).
        /// @return Success or an error code.
        [[nodiscard]] task_returning_expected_void_t process() noexcept(false);

        /// @brief Connects to a peer and creates one client-initiated stream per payload.
        /// @param params The peer, the SNI hostname, the payloads, the SSL_CTX and the QUIC settings; taken by value,
        ///               as the coroutine outlives the caller's temporary.
        /// @return Success or an error code once connection is established.
        [[nodiscard]] task_returning_expected_void_t connect(const connect_params params) noexcept(false);

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };
}

#endif // KMX_AIO_FEATURE_QUIC
