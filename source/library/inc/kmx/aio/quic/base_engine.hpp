/// @file aio/quic/base_engine.hpp
/// @brief Shared QUIC engine implementation factored out of the readiness and completion models.
/// @details This is a PRIVATE implementation detail — included only from the .cpp files.
///          It must NOT appear in any public header to avoid exposing lsquic.h to consumers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <arpa/inet.h>
    #include <array>
    #include <cerrno>
    #include <charconv>
    #include <cstddef>
    #include <cstdlib>
    #include <expected>
    #include <functional>
    #include <memory>
    #include <netinet/in.h>
    #include <optional>
    #include <queue>
    #include <source_location>
    #include <span>
    #include <string>
    #include <string_view>
    #include <sys/socket.h>
    #include <system_error>
    #include <unordered_set>
    #include <vector>
#endif

extern "C"
{
#include <lsquic.h>
}

#include <kmx/aio/basic_types.hpp>
#include <kmx/aio/buffer/pool.hpp>
#include <kmx/aio/quic/engine.hpp>
#include <kmx/aio/quic/settings.hpp>
#include <kmx/aio/readiness/descriptor/timer.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::quic
{
    namespace logger = ::kmx::logger;

    namespace detail
    {
        /// @brief Reads the readiness-model watchdog tick period from the `KMX_AIO_QUIC_TICK_NS` environment variable.
        /// @return The tick period in nanoseconds, or the built-in default when the variable is unset or malformed.
        long readiness_watchdog_tick_ns_from_env() noexcept;

        /// @brief Enables lsquic's internal debug logging when the corresponding environment variable is set.
        /// @note Called once during engine initialisation; a no-op when debug logging is not requested.
        void maybe_enable_lsquic_debug_logging() noexcept;

        /// @brief Converts an lsquic connection status into a human-readable string for logging.
        /// @param status The lsquic connection status to describe.
        /// @return A static, null-terminated view naming the status; "unknown" for unrecognised values.
        std::string_view conn_status_to_string(const ::LSQUIC_CONN_STATUS status) noexcept;

        /// @brief Populates an `lsquic_stream_if` callback table with the supplied handlers.
        /// @param stream_if     The callback table to fill in.
        /// @param on_new_conn   Invoked when a new connection is created.
        /// @param on_conn_closed Invoked when a connection is closed.
        /// @param on_new_stream Invoked when a new stream is created.
        /// @param on_read       Invoked when a stream becomes readable.
        /// @param on_write      Invoked when a stream becomes writable.
        /// @param on_close      Invoked when a stream is closed.
        /// @param on_hsk_done   Invoked when the TLS handshake completes.
        void configure_stream_if(::lsquic_stream_if& stream_if, ::lsquic_conn_ctx_t* (*on_new_conn)(void*, ::lsquic_conn_t*),
                                 void (*on_conn_closed)(::lsquic_conn_t*),
                                 ::lsquic_stream_ctx_t* (*on_new_stream)(void*, ::lsquic_stream_t*),
                                 void (*on_read)(::lsquic_stream_t*, ::lsquic_stream_ctx_t*),
                                 void (*on_write)(::lsquic_stream_t*, ::lsquic_stream_ctx_t*),
                                 void (*on_close)(::lsquic_stream_t*, ::lsquic_stream_ctx_t*),
                                 void (*on_hsk_done)(::lsquic_conn_t*, enum lsquic_hsk_status)) noexcept;

        /// @brief Translates the portable engine settings into lsquic's native settings structure.
        /// @param lsquic_settings The lsquic settings structure to populate.
        /// @param config          The portable QUIC settings to apply.
        /// @param lsquic_flags    The lsquic engine flags (`LSENG_SERVER`, `LSENG_HTTP`, ...) the settings are validated against.
        void apply_lsquic_settings(::lsquic_engine_settings& lsquic_settings, const kmx::aio::quic::settings& config,
                                   const unsigned lsquic_flags) noexcept;

        /// @brief Tells whether a stream was initiated locally rather than by the peer.
        /// @param stream    The stream to classify.
        /// @param is_client `true` when this endpoint is the client, `false` for a server.
        /// @return `true` if the stream id encodes a locally initiated stream.
        bool is_local_initiated_stream(const ::lsquic_stream_t* stream, const bool is_client) noexcept;

        /// @brief Writes a batch of outgoing lsquic packets to a UDP socket.
        /// @param fd    The bound UDP socket descriptor.
        /// @param specs The array of packet specifications lsquic wants sent.
        /// @param count The number of entries in @p specs.
        /// @return The number of packets actually sent; `-1` on failure with `errno` set.
        int send_packets_out_fd(const int fd, const ::lsquic_out_spec* specs, unsigned count) noexcept;
    } // namespace detail

    /// @brief Common QUIC engine implementation shared between readiness and completion models.
    /// @tparam Executor  The executor type (readiness::executor or completion::executor).
    /// @tparam UdpSocket The UDP socket type (readiness::udp::socket or completion::udp::socket).
    template <typename Executor, typename UdpSocket>
    struct base_impl
    {
        /// @brief Alias for the lsquic connection status type.
        using connection_status_t = ::LSQUIC_CONN_STATUS;

        /// @brief Number of pooled buffers reserved for inbound stream payloads.
        static constexpr std::size_t stream_payload_pool_capacity = 1024u;

        /// @brief The executor driving this engine's I/O.
        Executor& exec_;
        /// @brief User callback invoked for each fully received inbound stream payload.
        std::function<task<void>(::lsquic_stream_t*, stream_payload)> stream_handler_;
        /// @brief Fixed-capacity pool supplying the buffers handed to @ref stream_handler_.
        kmx::aio::buffer::pool<stream_payload_buffer, stream_payload_pool_capacity> stream_payload_pool_ {};
        /// @brief The bound UDP socket carrying all QUIC datagrams.
        std::unique_ptr<UdpSocket> socket_;
        /// @brief The underlying lsquic engine; owned and destroyed by this object.
        ::lsquic_engine_t* lsquic_engine_ {};
        /// @brief The local socket address, resolved after bind so ephemeral ports are reported correctly.
        sockaddr_storage local_addr_ {};
        /// @brief Borrowed OpenSSL `SSL_CTX` used for the TLS handshake; not owned.
        void* ssl_ctx_ {};
        /// @brief Set while @ref process is running; cleared to make the event loop exit.
        bool running_ {};
        /// @brief `true` for a client engine, `false` for a server engine.
        bool is_client_ {false};
        /// @brief The ALPN protocol identifier advertised during the handshake.
        std::string alpn_ {"kmx-aio"};
        /// @brief Payloads queued by the client, each sent on its own stream once the handshake completes.
        std::queue<std::string> client_payloads_ {};
        /// @brief Number of @ref client_payloads_ streams requested but not yet created by lsquic.
        std::size_t client_payload_streams_pending_ {};
        /// @brief Number of extra streams to open right after the handshake, independent of @ref client_payloads_.
        std::size_t post_handshake_stream_count_ {};
        /// @brief Number of post-handshake streams requested but not yet created by lsquic.
        std::size_t post_handshake_streams_pending_ {};
        /// @brief The post-handshake streams awaiting their first write.
        std::unordered_set<::lsquic_stream_t*> post_handshake_streams_ {};
        /// @brief Optional callback that writes the initial payload of a post-handshake stream.
        std::function<void(::lsquic_stream_t*)> post_handshake_stream_writer_;
        /// @brief Watchdog tick period, in nanoseconds, used by the readiness-model idle path.
        const long readiness_idle_tick_ns_ {detail::readiness_watchdog_tick_ns_from_env()};

        /// @brief Constructs an engine bound to an executor.
        /// @param exec The executor that will drive the engine's socket and timers.
        explicit base_impl(Executor& exec) noexcept: exec_(exec) {}

        /// @brief Destroys the lsquic engine and releases lsquic's global state.
        ~base_impl() noexcept
        {
            if (lsquic_engine_)
                ::lsquic_engine_destroy(lsquic_engine_);

            ::lsquic_global_cleanup();
        }

        // lsquic C callbacks

        /// @brief lsquic callback: writes a batch of outgoing packets to the engine's UDP socket.
        /// @param ctx   The owning @ref base_impl, passed through as `ea_packets_out_ctx`.
        /// @param specs The packet specifications lsquic wants sent.
        /// @param count The number of entries in @p specs.
        /// @return The number of packets actually sent.
        static int send_packets_out(void* ctx, const ::lsquic_out_spec* specs, const unsigned count)
        {
            auto* const self = static_cast<base_impl*>(ctx);
            return detail::send_packets_out_fd(self->socket_->get_fd(), specs, count);
        }

        /// @brief lsquic callback: associates a newly created connection with this engine.
        /// @param stream_if_ctx The owning @ref base_impl.
        /// @param conn          The new connection (unused).
        /// @return The per-connection context, which is the owning @ref base_impl.
        static ::lsquic_conn_ctx_t* on_new_conn(void* stream_if_ctx, ::lsquic_conn_t* conn)
        {
            (void) conn;
            return reinterpret_cast<::lsquic_conn_ctx_t*>(stream_if_ctx);
        }

        /// @brief lsquic callback: logs the close reason and stops a client engine's event loop.
        /// @param conn The connection being closed.
        static void on_conn_closed(::lsquic_conn_t* conn)
        {
            auto* const self = reinterpret_cast<base_impl*>(::lsquic_conn_get_ctx(conn));
            std::array<char, 512u> errbuf {};
            const auto status = ::lsquic_conn_status(conn, errbuf.data(), errbuf.size());
            logger::log(logger::level::info, std::source_location::current(),
                        "[QUIC DEBUG] on_conn_closed called, status={} ({}), reason='{}'", static_cast<int>(status),
                        detail::conn_status_to_string(status), errbuf.data());

            if (self && self->is_client_)
                self->running_ = false;

            ::lsquic_conn_set_ctx(conn, nullptr);
        }

        /// @brief lsquic callback: opens the queued client and post-handshake streams once TLS completes.
        /// @param conn   The connection whose handshake finished.
        /// @param status The handshake outcome reported by lsquic.
        static void on_hsk_done(::lsquic_conn_t* conn, enum lsquic_hsk_status status)
        {
            auto* const self = reinterpret_cast<base_impl*>(::lsquic_conn_get_ctx(conn));
            logger::log(logger::level::info, std::source_location::current(), "[QUIC DEBUG] on_hsk_done called, status={}, is_client_={}",
                        static_cast<int>(status), self ? self->is_client_ : false);

            if (self)
            {
                if (self->is_client_)
                {
                    logger::log(logger::level::info, std::source_location::current(),
                                "[QUIC DEBUG] on_hsk_done: client handshake completed");

                    const std::size_t streams_to_open = self->client_payloads_.size();
                    self->client_payload_streams_pending_ += streams_to_open;
                    for (std::size_t i = 0; i < streams_to_open; ++i)
                        ::lsquic_conn_make_stream(conn);
                }

                if (self->post_handshake_stream_count_ > 0u)
                {
                    self->post_handshake_streams_pending_ += self->post_handshake_stream_count_;
                    for (std::size_t i = 0; i < self->post_handshake_stream_count_; ++i)
                        ::lsquic_conn_make_stream(conn);
                }
            }
        }

        /// @brief lsquic callback: arms read or write interest on a newly created stream.
        /// @details Locally initiated streams start out writable and consume one pending-stream slot;
        ///          peer-initiated streams start out readable.
        /// @param stream_if_ctx The owning @ref base_impl.
        /// @param stream        The newly created stream.
        /// @return The per-stream context, which is the owning @ref base_impl.
        static ::lsquic_stream_ctx_t* on_new_stream(void* stream_if_ctx, ::lsquic_stream_t* stream)
        {
            auto* const self = static_cast<base_impl*>(stream_if_ctx);
            const bool is_local_stream = detail::is_local_initiated_stream(stream, self->is_client_);

            if (is_local_stream)
            {
                if (self->client_payload_streams_pending_ > 0u)
                {
                    --self->client_payload_streams_pending_;
                }
                else if (self->post_handshake_streams_pending_ > 0u)
                {
                    --self->post_handshake_streams_pending_;
                    self->post_handshake_streams_.insert(stream);
                }

                ::lsquic_stream_wantwrite(stream, 1);
            }
            else
                ::lsquic_stream_wantread(stream, 1);
            return reinterpret_cast<::lsquic_stream_ctx_t*>(stream_if_ctx);
        }

        /// @brief lsquic callback: drains a readable stream and dispatches the payload to @ref stream_handler_.
        /// @param stream The readable stream.
        static void on_read(::lsquic_stream_t* stream, ::lsquic_stream_ctx_t* /*ctx*/)
        {
            auto* const self = reinterpret_cast<base_impl*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(stream)));

            auto handle_read_result = [&](const ssize_t nr) -> void
            {
                if (nr == 0)
                    ::lsquic_stream_wantread(stream, 0);
            };

            if (!self->stream_handler_)
            {
                std::array<char, stream_payload_capacity> scratch {};
                handle_read_result(::lsquic_stream_read(stream, scratch.data(), scratch.size()));
                return;
            }

            buffer::handle<stream_payload_buffer> payload_storage;
            try
            {
                payload_storage = self->stream_payload_pool_.acquire();
            }
            catch (const std::exception&)
            {
                std::array<char, stream_payload_capacity> scratch {};
                const ssize_t nr = ::lsquic_stream_read(stream, scratch.data(), scratch.size());
                if (nr > 0)
                {
                    logger::log(logger::level::warn, std::source_location::current(), "QUIC payload pool exhausted; dropping {} byte(s)",
                                static_cast<std::size_t>(nr));
                    return;
                }

                handle_read_result(nr);
                return;
            }

            const ssize_t nr = ::lsquic_stream_read(stream, payload_storage->data(), payload_storage->size());
            if (nr > 0)
            {
                self->exec_.spawn(self->stream_handler_(stream, stream_payload {std::move(payload_storage), static_cast<std::size_t>(nr)}));
                return;
            }

            handle_read_result(nr);
        }

        /// @brief lsquic callback: writes the next queued payload to a writable stream.
        /// @details Post-handshake streams are handed to @ref post_handshake_stream_writer_; otherwise a client
        ///          pops one entry from @ref client_payloads_, writes it, and half-closes the stream.
        /// @param stream The writable stream.
        static void on_write(::lsquic_stream_t* stream, ::lsquic_stream_ctx_t* /*ctx*/)
        {
            auto* const self = reinterpret_cast<base_impl*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(stream)));
            if (self->is_client_)
            {
                const auto bootstrap_it = self->post_handshake_streams_.find(stream);
                if (bootstrap_it != self->post_handshake_streams_.end())
                {
                    if (self->post_handshake_stream_writer_)
                    {
                        try
                        {
                            self->post_handshake_stream_writer_(stream);
                        }
                        catch (const std::exception& ex)
                        {
                            logger::log(logger::level::error, std::source_location::current(), "Post-handshake stream writer failed: {}",
                                        ex.what());
                        }
                    }

                    self->post_handshake_streams_.erase(bootstrap_it);
                    ::lsquic_stream_wantwrite(stream, 0);
                    ::lsquic_stream_wantread(stream, 1);
                    return;
                }
            }

            if (self->is_client_ && !self->client_payloads_.empty())
            {
                std::string payload = std::move(self->client_payloads_.front());
                self->client_payloads_.pop();

                std::size_t written {};
                while (written < payload.size())
                {
                    const ssize_t chunk = ::lsquic_stream_write(stream, payload.data() + written, payload.size() - written);
                    if (chunk <= 0)
                    {
                        logger::log(logger::level::warn, std::source_location::current(),
                                    "QUIC client write failed on stream {}, written={}/{}",
                                    static_cast<unsigned long long>(::lsquic_stream_id(stream)), written, payload.size());
                        break;
                    }

                    written += static_cast<std::size_t>(chunk);
                }

                ::lsquic_stream_flush(stream);
                ::lsquic_stream_shutdown(stream, 1);
                ::lsquic_stream_wantwrite(stream, 0);
                ::lsquic_stream_wantread(stream, 1);
            }
            else
                ::lsquic_stream_wantwrite(stream, 0);
        }

        /// @brief lsquic callback: supplies the `SSL_CTX` used for a connection.
        /// @param peer_ctx The owning @ref base_impl.
        /// @return The borrowed `SSL_CTX` stored in @ref ssl_ctx_.
        static struct ssl_ctx_st* get_ssl_ctx(void* peer_ctx, const struct sockaddr* /*local*/)
        {
            auto* const self = static_cast<base_impl*>(peer_ctx);
            return reinterpret_cast<struct ssl_ctx_st*>(self->ssl_ctx_);
        }

        /// @brief lsquic callback: supplies the server certificate context for an incoming connection.
        /// @param cert_lu_ctx The owning @ref base_impl.
        /// @return The borrowed `SSL_CTX` stored in @ref ssl_ctx_.
        static struct ssl_ctx_st* lookup_cert(void* cert_lu_ctx, const struct sockaddr* /*local*/, const char* /*sni*/)
        {
            auto* const self = static_cast<base_impl*>(cert_lu_ctx);
            return reinterpret_cast<struct ssl_ctx_st*>(self->ssl_ctx_);
        }

        /// @brief lsquic callback: drops a closed stream from the post-handshake bookkeeping.
        /// @param stream The stream being closed.
        static void on_close(::lsquic_stream_t* stream, ::lsquic_stream_ctx_t* /*ctx*/)
        {
            auto* const self = reinterpret_cast<base_impl*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(stream)));
            if (!self)
                return;

            self->post_handshake_streams_.erase(stream);
        }

        // Shared initialisation

        /// @brief Configures lsquic callbacks, settings, and creates the lsquic_engine.
        /// @return Success or an error code.
        [[nodiscard]] expected_void_t init_lsquic(const kmx::aio::quic::settings& config, unsigned lsquic_flags)
        {
            detail::maybe_enable_lsquic_debug_logging();

            if (::lsquic_global_init(lsquic_flags & LSENG_SERVER ? LSQUIC_GLOBAL_SERVER : LSQUIC_GLOBAL_CLIENT) != 0)
                return std::unexpected(error_from_errno(EINVAL));

            static ::lsquic_stream_if stream_if {};
            detail::configure_stream_if(stream_if, on_new_conn, on_conn_closed, on_new_stream, on_read, on_write, on_close, on_hsk_done);

            ::lsquic_engine_api engine_api {};
            engine_api.ea_packets_out = send_packets_out;
            engine_api.ea_packets_out_ctx = this;
            engine_api.ea_stream_if = &stream_if;
            engine_api.ea_stream_if_ctx = this;
            engine_api.ea_lookup_cert = lookup_cert;
            engine_api.ea_cert_lu_ctx = this;
            engine_api.ea_get_ssl_ctx = get_ssl_ctx;
            engine_api.ea_alpn = alpn_.c_str();

            static ::lsquic_engine_settings lsquic_settings {};
            detail::apply_lsquic_settings(lsquic_settings, config, lsquic_flags);
            engine_api.ea_settings = &lsquic_settings;

            lsquic_engine_ = ::lsquic_engine_new(lsquic_flags, &engine_api);
            if (!lsquic_engine_)
                return std::unexpected(error_from_errno(EINVAL));

            return {};
        }

        /// @brief Binds the UDP socket and stores the local address.
        /// @return Success or an error code.
        [[nodiscard]] expected_void_t bind_socket(const ip_address_t ip, const port_t port)
        {
            auto sock_addr_result = make_socket_address(ip, port);
            if (!sock_addr_result)
                return std::unexpected(sock_addr_result.error());

            if (::bind(socket_->get_fd(), reinterpret_cast<sockaddr*>(&sock_addr_result->storage), sock_addr_result->length) < 0)
                return std::unexpected(error_from_errno());

            // For ephemeral binds (port 0), propagate the kernel-assigned local address to lsquic.
            ::socklen_t local_len = sizeof(local_addr_);
            if (::getsockname(socket_->get_fd(), reinterpret_cast<sockaddr*>(&local_addr_), &local_len) < 0)
                return std::unexpected(error_from_errno());

            return {};
        }

        /// @brief Shared initialisation logic called after model-specific socket creation.
        [[nodiscard]] expected_void_t setup(std::expected<UdpSocket, std::error_code>&& sock_res, const ip_address_t ip, const port_t port,
                                            void* ssl_ctx, const kmx::aio::quic::settings& config)
        {
            if (!sock_res)
                return std::unexpected(sock_res.error());

            ssl_ctx_ = ssl_ctx;
            socket_ = std::make_unique<UdpSocket>(std::move(*sock_res));

            if (auto bind_res = bind_socket(ip, port); !bind_res)
                return std::unexpected(bind_res.error());

            if (auto init_res = init_lsquic(config, LSENG_SERVER); !init_res)
                return std::unexpected(init_res.error());

            return {};
        }

        /// @brief Prepares a client engine that sends a single payload once the handshake completes.
        /// @param sock_res       The freshly created UDP socket, or the error that creating it produced.
        /// @param peer_ip        The server IP address to connect to.
        /// @param peer_port      The server UDP port to connect to.
        /// @param hostname       The SNI hostname; empty to omit SNI.
        /// @param client_payload The payload to send; ignored when empty.
        /// @param ssl_ctx        The borrowed OpenSSL `SSL_CTX` to use for the handshake.
        /// @param config         The QUIC settings to apply to the engine.
        /// @return Success, or an error code if the socket, bind, engine, or connect step failed.
        [[nodiscard]] expected_void_t connect_setup(std::expected<UdpSocket, std::error_code>&& sock_res, const ip_address_t peer_ip,
                                                    const port_t peer_port, const std::string& hostname, const std::string& client_payload,
                                                    void* ssl_ctx, const kmx::aio::quic::settings& config)
        {
            clear_client_payload_queue();

            if (!client_payload.empty())
                client_payloads_.push(client_payload);
            return connect_setup_common(std::move(sock_res), peer_ip, peer_port, hostname, ssl_ctx, config);
        }

        /// @brief Prepares a client engine that sends each payload on its own stream once the handshake completes.
        /// @param sock_res        The freshly created UDP socket, or the error that creating it produced.
        /// @param peer_ip         The server IP address to connect to.
        /// @param peer_port       The server UDP port to connect to.
        /// @param hostname        The SNI hostname; empty to omit SNI.
        /// @param client_payloads The payloads to send; empty entries are skipped.
        /// @param ssl_ctx         The borrowed OpenSSL `SSL_CTX` to use for the handshake.
        /// @param config          The QUIC settings to apply to the engine.
        /// @return Success, or an error code if the socket, bind, engine, or connect step failed.
        [[nodiscard]] expected_void_t connect_setup(std::expected<UdpSocket, std::error_code>&& sock_res, const ip_address_t peer_ip,
                                                    const port_t peer_port, const std::string& hostname,
                                                    const std::vector<std::string>& client_payloads, void* ssl_ctx,
                                                    const kmx::aio::quic::settings& config)
        {
            clear_client_payload_queue();

            for (const auto& payload: client_payloads)
                if (!payload.empty())
                    client_payloads_.push(payload);

            return connect_setup_common(std::move(sock_res), peer_ip, peer_port, hostname, ssl_ctx, config);
        }

    private:
        /// @brief Discards any payloads left over from a previous connection attempt.
        void clear_client_payload_queue() noexcept
        {
            while (!client_payloads_.empty())
                client_payloads_.pop();
        }

        /// @brief Binds an ephemeral port, creates the lsquic engine, and initiates the client connection.
        /// @param sock_res  The freshly created UDP socket, or the error that creating it produced.
        /// @param peer_ip   The server IP address to connect to.
        /// @param peer_port The server UDP port to connect to.
        /// @param hostname  The SNI hostname; empty to omit SNI.
        /// @param ssl_ctx   The borrowed OpenSSL `SSL_CTX` to use for the handshake.
        /// @param config    The QUIC settings to apply to the engine.
        /// @return Success, or an error code if any setup step failed.
        [[nodiscard]] expected_void_t connect_setup_common(std::expected<UdpSocket, std::error_code>&& sock_res, const ip_address_t peer_ip,
                                                           const port_t peer_port, const std::string& hostname, void* ssl_ctx,
                                                           const kmx::aio::quic::settings& config)
        {
            if (!sock_res)
                return std::unexpected(sock_res.error());

            ssl_ctx_ = ssl_ctx;
            is_client_ = true;
            client_payload_streams_pending_ = 0u;
            post_handshake_streams_pending_ = 0u;
            post_handshake_streams_.clear();

            socket_ = std::make_unique<UdpSocket>(std::move(*sock_res));

            // Bind to ephemeral port
            static constexpr std::array<std::uint8_t, 4u> any_ip {0, 0, 0, 0};
            if (auto bind_res = bind_socket(any_ip, 0); !bind_res)
                return std::unexpected(bind_res.error());

            if (auto init_res = init_lsquic(config, 0); !init_res)
                return std::unexpected(init_res.error());

            auto peer_addr_result = make_socket_address(peer_ip, peer_port);
            if (!peer_addr_result)
                return std::unexpected(peer_addr_result.error());

            if (::connect(socket_->get_fd(), reinterpret_cast<sockaddr*>(&peer_addr_result->storage), peer_addr_result->length) < 0)
                return std::unexpected(error_from_errno());

            ::socklen_t local_len = sizeof(local_addr_);
            if (::getsockname(socket_->get_fd(), reinterpret_cast<sockaddr*>(&local_addr_), &local_len) < 0)
                return std::unexpected(error_from_errno());

            const char* host = hostname.empty() ? nullptr : hostname.c_str();

            ::lsquic_conn_t* const conn = ::lsquic_engine_connect(lsquic_engine_, N_LSQVER, reinterpret_cast<sockaddr*>(&local_addr_),
                                                                  reinterpret_cast<sockaddr*>(&peer_addr_result->storage),
                                                                  static_cast<void*>(this), nullptr, host, 0, nullptr, 0, nullptr, 0);
            if (!conn)
                return std::unexpected(error_from_errno());

            return {};
        }

        /// @brief Scope guard that unregisters the readiness watchdog timer when @ref process returns.
        struct timer_guard_t
        {
            /// @brief The executor the timer descriptor is registered with.
            Executor& exec;
            /// @brief The watchdog timer to unregister; empty in the completion model.
            std::optional<kmx::aio::readiness::descriptor::timer>& tick;

            /// @brief Unregisters the watchdog timer if one was created.
            ~timer_guard_t() noexcept
            {
                if (tick && tick->is_valid())
                    if constexpr (requires(Executor& e) { e.unregister_fd(0); })
                        exec.unregister_fd(tick->get());
            }
        };

        /// @brief Points a `msghdr` at the packet buffer so it can be reused across `recvmsg` calls.
        /// @param packet_buf The buffer receiving the datagram.
        /// @param peer_addr  Storage for the sender address.
        /// @param msg        The message header to initialise.
        /// @param iov        The single-entry scatter/gather array backing @p msg.
        static void prepare_recv_message(std::array<std::byte, 4096u>& packet_buf, ::sockaddr_storage& peer_addr, ::msghdr& msg,
                                         ::iovec (&iov)[1u]) noexcept
        {
            iov[0].iov_base = packet_buf.data();
            iov[0].iov_len = packet_buf.size();
            msg.msg_name = &peer_addr;
            msg.msg_namelen = sizeof(peer_addr);
            msg.msg_iov = iov;
            msg.msg_iovlen = 1;
        }

        /// @brief Runs one lsquic processing pass and flushes any packets it produced.
        void drive_engine_once() noexcept
        {
            ::lsquic_engine_process_conns(lsquic_engine_);
            ::lsquic_engine_send_unsent_packets(lsquic_engine_);
        }

        /// @brief Drives the engine a few times up front so a client's initial packets leave before the first receive.
        /// @note The iteration count is a heuristic: lsquic may need several passes to emit the full Initial flight.
        void bootstrap_initial_packets() noexcept
        {
            for (int i = 0; i < 10; ++i)
                drive_engine_once();
        }

        /// @brief Creates and registers the readiness watchdog timer, if the executor lacks a native timeout.
        /// @param readiness_tick Receives the created timer; left empty for completion-model executors.
        /// @return Success, or an error code if the timer could not be created or registered.
        [[nodiscard]] expected_void_t setup_readiness_timer_if_needed(std::optional<kmx::aio::readiness::descriptor::timer>& readiness_tick)
        {
            if constexpr (requires(Executor& e) { e.async_timeout(std::uint64_t {}); })
                return {};
            else
            {
                auto timer_res = kmx::aio::readiness::descriptor::timer::create();
                if (!timer_res)
                    return std::unexpected(timer_res.error());

                if (auto reg_res = exec_.register_fd(timer_res->get()); !reg_res)
                    return std::unexpected(reg_res.error());

                readiness_tick.emplace(std::move(*timer_res));
                return {};
            }
        }

        /// @brief Hands a received datagram to lsquic and drives the engine once.
        /// @param packet_buf The buffer holding the datagram.
        /// @param recv_n     The number of bytes received.
        /// @param peer_addr  The address the datagram came from.
        /// @return Success, or an error code if lsquic rejected the packet.
        [[nodiscard]] expected_void_t feed_packet_to_engine(const std::array<std::byte, 4096u>& packet_buf, const ssize_t recv_n,
                                                            const ::sockaddr_storage& peer_addr)
        {
            const int packet_in_res = ::lsquic_engine_packet_in(
                lsquic_engine_, reinterpret_cast<const unsigned char*>(packet_buf.data()), static_cast<std::size_t>(recv_n),
                reinterpret_cast<::sockaddr*>(&local_addr_), reinterpret_cast<::sockaddr*>(const_cast<::sockaddr_storage*>(&peer_addr)),
                reinterpret_cast<void*>(this), 0);
            if (packet_in_res < 0)
            {
                logger::log(logger::level::error, std::source_location::current(), "lsquic_engine_packet_in failed: {}", packet_in_res);
                return std::unexpected(error_from_errno(EPROTO));
            }

            drive_engine_once();
            return {};
        }

        /// @brief Suspends for one idle tick using the completion executor's native timeout.
        /// @return Success, or an error code if the timeout failed.
        task_returning_expected_void_t wait_completion_idle_tick()
        {
            auto timeout_res = co_await exec_.async_timeout(1'000'000ULL); // 1 ms
            if (!timeout_res)
                co_return std::unexpected(timeout_res.error());

            co_return expected_void_t {};
        }

        /// @brief Suspends for one idle tick by arming and awaiting the readiness watchdog timer.
        /// @param readiness_tick The watchdog timer created by @ref setup_readiness_timer_if_needed.
        /// @return Success, or an error code if the timer could not be armed or awaited.
        task_returning_expected_void_t wait_readiness_idle_tick(kmx::aio::readiness::descriptor::timer& readiness_tick)
        {
            ::itimerspec one_ms {};
            one_ms.it_value.tv_nsec = readiness_idle_tick_ns_;

            if (auto arm_res = readiness_tick.set_time(0, one_ms); !arm_res)
                co_return std::unexpected(arm_res.error());

            auto tick_res = co_await readiness_tick.wait(exec_);
            if (!tick_res)
                co_return std::unexpected(tick_res.error());

            co_return expected_void_t {};
        }

        /// @brief Performs one receive-and-feed iteration of the completion-model event loop.
        /// @param packet_buf The buffer receiving the datagram.
        /// @param msg        The message header prepared by @ref prepare_recv_message.
        /// @param peer_addr  The address the datagram came from.
        /// @return Success, or an error code; a would-block receive idles instead of failing.
        task_returning_expected_void_t process_completion_receive_iteration(std::array<std::byte, 4096u>& packet_buf, ::msghdr& msg,
                                                                            const ::sockaddr_storage& peer_addr)
        {
            const ssize_t recv_n = ::recvmsg(socket_->get_fd(), &msg, MSG_DONTWAIT);
            if (recv_n < 0)
            {
                if (would_block(errno))
                {
                    auto idle_res = co_await wait_completion_idle_tick();
                    if (!idle_res)
                        co_return std::unexpected(idle_res.error());

                    co_return expected_void_t {};
                }

                co_return std::unexpected(error_from_errno());
            }

            if (recv_n > 0)
                if (auto packet_res = feed_packet_to_engine(packet_buf, recv_n, peer_addr); !packet_res)
                    co_return std::unexpected(packet_res.error());

            co_return expected_void_t {};
        }

        /// @brief Performs one receive-and-feed iteration of the readiness-model event loop.
        /// @param packet_buf    The buffer receiving the datagram.
        /// @param msg           The message header prepared by @ref prepare_recv_message.
        /// @param peer_addr     The address the datagram came from.
        /// @param readiness_tick The watchdog timer used while the socket has nothing to read.
        /// @return Success, or an error code; a would-block receive idles instead of failing.
        task_returning_expected_void_t process_readiness_receive_iteration(std::array<std::byte, 4096u>& packet_buf, ::msghdr& msg,
                                                                           const ::sockaddr_storage& peer_addr,
                                                                           kmx::aio::readiness::descriptor::timer& readiness_tick)
        {
            const ssize_t recv_n = ::recvmsg(socket_->get_fd(), &msg, MSG_DONTWAIT);
            if (recv_n < 0)
            {
                if (would_block(errno))
                {
                    auto idle_res = co_await wait_readiness_idle_tick(readiness_tick);
                    if (!idle_res)
                        co_return std::unexpected(idle_res.error());

                    co_return expected_void_t {};
                }

                co_return std::unexpected(error_from_errno());
            }

            if (recv_n > 0)
                if (auto packet_res = feed_packet_to_engine(packet_buf, recv_n, peer_addr); !packet_res)
                    co_return std::unexpected(packet_res.error());

            co_return expected_void_t {};
        }

    public:
        /// @brief Shared event processing loop.
        task_returning_expected_void_t process()
        {
            running_ = true;
            std::array<std::byte, 4096u> packet_buf {};
            ::msghdr msg {};
            ::iovec iov[1u] {};
            std::optional<kmx::aio::readiness::descriptor::timer> readiness_tick;
            [[maybe_unused]] timer_guard_t timer_guard {exec_, readiness_tick};

            if (auto setup_res = setup_readiness_timer_if_needed(readiness_tick); !setup_res)
                co_return std::unexpected(setup_res.error());

            bootstrap_initial_packets();

            while (running_)
            {
                drive_engine_once();

                ::sockaddr_storage peer_addr {};
                prepare_recv_message(packet_buf, peer_addr, msg, iov);

                if constexpr (requires(Executor& e) { e.async_timeout(std::uint64_t {}); })
                {
                    auto iter_res = co_await process_completion_receive_iteration(packet_buf, msg, peer_addr);
                    if (!iter_res)
                        co_return std::unexpected(iter_res.error());
                }
                else
                {
                    auto iter_res = co_await process_readiness_receive_iteration(packet_buf, msg, peer_addr, *readiness_tick);
                    if (!iter_res)
                        co_return std::unexpected(iter_res.error());
                }
            }

            co_return expected_void_t {};
        }
    };

} // namespace kmx::aio::quic
