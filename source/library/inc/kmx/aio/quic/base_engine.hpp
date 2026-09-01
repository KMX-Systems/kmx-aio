/// @file aio/quic/base_engine.hpp
/// @brief Shared QUIC engine implementation factored out of the readiness and completion models.
/// @details This is a PRIVATE implementation detail — included only from the .cpp files.
///          It must NOT appear in any public header to avoid exposing lsquic.h to consumers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <arpa/inet.h>
    #include <array>
    #include <atomic>
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
    #include <thread>
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
#include <kmx/aio/quic/read_park_list.hpp>
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

    /// @brief Non-template core of the QUIC engine, shared verbatim by every @ref base_impl instantiation.
    /// @details Holds every piece of engine state that does not depend on the executor or socket types, together
    ///          with the lsquic C callbacks, the engine and socket setup steps, and the packet-pump helpers. All of
    ///          it is compiled once, in base_engine.cpp, instead of once per executor/socket pair.
    /// @note The two operations that genuinely need the derived type are published by @ref base_impl: the socket
    ///       descriptor through @ref socket_fd_, and coroutine spawning through the @ref spawn_stream_task_ thunk.
    /// @warning An lsquic engine carries no internal locking, and neither does this object: its connection tables,
    ///          its payload queues and its pending-stream counters are plain members, deliberately. Everything that
    ///          touches the engine - the packet pump, the stream callbacks lsquic makes back into this object, and
    ///          engine teardown - must therefore run on the one thread that created it, which is the thread whose
    ///          executor drives @ref base_impl::process. Nothing about a violation is visible at the point it
    ///          happens: the engine corrupts its own state and the connection fails later, somewhere else. So the
    ///          engine records that thread at creation and @ref check_engine_thread names the first call that comes
    ///          from another one, in every build. Calling from another thread is not made safe by that - it is only
    ///          made visible. Work that arises elsewhere belongs on the executor, not on the engine directly.
    struct primary_base_impl
    {
        /// @brief Alias for the lsquic connection status type.
        using connection_status_t = ::LSQUIC_CONN_STATUS;

        /// @brief Signature of the thunk that hands a completed inbound payload to the derived engine's executor.
        using spawn_stream_task_t = void (*)(primary_base_impl&, ::lsquic_stream_t*, stream_payload);

        /// @brief Number of pooled buffers reserved for inbound stream payloads.
        static constexpr std::size_t stream_payload_pool_capacity = 1024u;

        /// @brief Size of the buffer receiving a single inbound datagram.
        static constexpr std::size_t packet_buffer_capacity = 4096u;

        /// @brief The buffer type receiving a single inbound datagram.
        using packet_buffer_t = std::array<std::byte, packet_buffer_capacity>;

        /// @brief User callback invoked for each fully received inbound stream payload.
        std::function<task<void>(::lsquic_stream_t*, stream_payload)> stream_handler_;
        /// @brief Fixed-capacity pool supplying the buffers handed to @ref stream_handler_.
        kmx::aio::buffer::pool<stream_payload_buffer, stream_payload_pool_capacity> stream_payload_pool_ {};
        /// @brief Spawns @ref stream_handler_ on the derived engine's executor; installed by @ref base_impl.
        const spawn_stream_task_t spawn_stream_task_ {};
        /// @brief The underlying lsquic engine; owned and destroyed by this object.
        ::lsquic_engine_t* lsquic_engine_ {};
        /// @brief The local socket address, resolved after bind so ephemeral ports are reported correctly.
        sockaddr_storage local_addr_ {};
        /// @brief Borrowed OpenSSL `SSL_CTX` used for the TLS handshake; not owned.
        void* ssl_ctx_ {};
        /// @brief Descriptor of the UDP socket owned by @ref base_impl; `-1` until the socket is adopted.
        int socket_fd_ {-1};
        /// @brief Set while @ref base_impl::process is running; cleared to make the event loop exit.
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
        /// @brief Streams whose read interest is parked because @ref stream_payload_pool_ had no free buffer.
        /// @details A parked stream is deliberately left unread. The bytes stay in lsquic's receive buffer, the
        ///          flow-control window closes behind them and the peer stops sending, which is what
        ///          backpressure is. @ref resume_parked_reads re-arms the streams once a buffer comes back, and
        ///          @ref on_close drops one that is closed while parked, so no entry outlives its stream.
        detail::read_park_list read_parked_streams_ {};
        /// @brief Watchdog tick period, in nanoseconds, used by the readiness-model idle path.
        const long readiness_idle_tick_ns_ {detail::readiness_watchdog_tick_ns_from_env()};
        /// @brief The thread the lsquic engine was created on: the only one allowed to drive it.
        /// @details Empty until @ref init_lsquic succeeds, which is why @ref check_engine_thread passes anything
        ///          that happens before there is an engine to misuse.
        std::thread::id engine_thread_ {};
        /// @brief Whether a call from a foreign thread has already been reported.
        /// @details Atomic because the threads that would set it are by definition racing. It exists so that a
        ///          violated affinity costs one log line rather than one per datagram.
        std::atomic_bool reported_foreign_thread_ {};

        /// @brief Constructs the engine core.
        /// @param spawn_stream_task The thunk spawning @ref stream_handler_ on the derived engine's executor.
        explicit primary_base_impl(const spawn_stream_task_t spawn_stream_task) noexcept;

        primary_base_impl(const primary_base_impl&) = delete;
        primary_base_impl(primary_base_impl&&) = delete;
        primary_base_impl& operator=(const primary_base_impl&) = delete;
        primary_base_impl& operator=(primary_base_impl&&) = delete;

        /// @brief Destroys the lsquic engine, if @ref base_impl has not already done so, and releases lsquic's global state.
        ~primary_base_impl() noexcept;

        // lsquic C callbacks

        /// @brief lsquic callback: writes a batch of outgoing packets to the engine's UDP socket.
        /// @param ctx   The owning @ref primary_base_impl, passed through as `ea_packets_out_ctx`.
        /// @param specs The packet specifications lsquic wants sent.
        /// @param count The number of entries in @p specs.
        /// @return The number of packets actually sent.
        static int send_packets_out(void* ctx, const ::lsquic_out_spec* specs, const unsigned count);

        /// @brief lsquic callback: associates a newly created connection with this engine.
        /// @param stream_if_ctx The owning @ref primary_base_impl.
        /// @param conn          The new connection (unused).
        /// @return The per-connection context, which is the owning @ref primary_base_impl.
        static ::lsquic_conn_ctx_t* on_new_conn(void* stream_if_ctx, ::lsquic_conn_t* conn);

        /// @brief lsquic callback: logs the close reason and stops a client engine's event loop.
        /// @param conn The connection being closed.
        static void on_conn_closed(::lsquic_conn_t* conn);

        /// @brief lsquic callback: opens the queued client and post-handshake streams once TLS completes.
        /// @param conn   The connection whose handshake finished.
        /// @param status The handshake outcome reported by lsquic.
        static void on_hsk_done(::lsquic_conn_t* conn, enum lsquic_hsk_status status);

        /// @brief lsquic callback: arms read or write interest on a newly created stream.
        /// @details Locally initiated streams start out writable and consume one pending-stream slot;
        ///          peer-initiated streams start out readable.
        /// @param stream_if_ctx The owning @ref primary_base_impl.
        /// @param stream        The newly created stream.
        /// @return The per-stream context, which is the owning @ref primary_base_impl.
        static ::lsquic_stream_ctx_t* on_new_stream(void* stream_if_ctx, ::lsquic_stream_t* stream);

        /// @brief lsquic callback: drains a readable stream and dispatches the payload to @ref stream_handler_.
        /// @details Reads only into a buffer leased from @ref stream_payload_pool_. When the pool is empty the
        ///          stream is parked in @ref read_parked_streams_ and nothing is read, so the unread bytes stay
        ///          where the peer can still account for them.
        /// @param stream The readable stream.
        static void on_read(::lsquic_stream_t* stream, ::lsquic_stream_ctx_t* ctx);

        /// @brief lsquic callback: writes the next queued payload to a writable stream.
        /// @details Post-handshake streams are handed to @ref post_handshake_stream_writer_; otherwise a client
        ///          pops one entry from @ref client_payloads_, writes it, and half-closes the stream.
        /// @param stream The writable stream.
        static void on_write(::lsquic_stream_t* stream, ::lsquic_stream_ctx_t* ctx);

        /// @brief lsquic callback: supplies the `SSL_CTX` used for a connection.
        /// @param peer_ctx The owning @ref primary_base_impl.
        /// @return The borrowed `SSL_CTX` stored in @ref ssl_ctx_.
        static struct ssl_ctx_st* get_ssl_ctx(void* peer_ctx, const struct sockaddr* local);

        /// @brief lsquic callback: supplies the server certificate context for an incoming connection.
        /// @param cert_lu_ctx The owning @ref primary_base_impl.
        /// @return The borrowed `SSL_CTX` stored in @ref ssl_ctx_.
        static struct ssl_ctx_st* lookup_cert(void* cert_lu_ctx, const struct sockaddr* local, const char* sni);

        /// @brief lsquic callback: drops a closed stream from the post-handshake and parked-read bookkeeping.
        /// @param stream The stream being closed.
        static void on_close(::lsquic_stream_t* stream, ::lsquic_stream_ctx_t* ctx);

        // Shared initialisation

        /// @brief Configures lsquic callbacks, settings, and creates the lsquic_engine.
        /// @param config       The portable QUIC settings to apply.
        /// @param lsquic_flags The lsquic engine flags (`LSENG_SERVER`, `LSENG_HTTP`, ...).
        /// @return Success or an error code.
        [[nodiscard]] expected_void_t init_lsquic(const kmx::aio::quic::settings& config, unsigned lsquic_flags);

        /// @brief Binds the UDP socket and stores the local address.
        /// @return Success or an error code.
        [[nodiscard]] expected_void_t bind_socket(const ip_address_t ip, const port_t port);

        /// @brief Binds the server socket and creates the lsquic engine, once @ref socket_fd_ is known.
        /// @param ip      The local IP address to bind to.
        /// @param port    The local UDP port to bind to.
        /// @param ssl_ctx The borrowed OpenSSL `SSL_CTX` to use for the handshake.
        /// @param config  The QUIC settings to apply to the engine.
        /// @return Success, or an error code if the bind or engine step failed.
        [[nodiscard]] expected_void_t setup_after_socket(const ip_address_t ip, const port_t port, void* ssl_ctx,
                                                         const kmx::aio::quic::settings& config);

        /// @brief Binds an ephemeral port, creates the lsquic engine, and initiates the client connection.
        /// @param peer_ip   The server IP address to connect to.
        /// @param peer_port The server UDP port to connect to.
        /// @param hostname  The SNI hostname; empty to omit SNI.
        /// @param ssl_ctx   The borrowed OpenSSL `SSL_CTX` to use for the handshake.
        /// @param config    The QUIC settings to apply to the engine.
        /// @return Success, or an error code if any setup step failed.
        [[nodiscard]] expected_void_t connect_setup_after_socket(const ip_address_t peer_ip, const port_t peer_port,
                                                                 const std::string& hostname, void* ssl_ctx,
                                                                 const kmx::aio::quic::settings& config);

        /// @brief Replaces the client payload queue with a single payload; an empty payload queues nothing.
        void set_client_payload(const std::string& payload);

        /// @brief Replaces the client payload queue with one entry per non-empty payload.
        void set_client_payloads(const std::vector<std::string>& payloads);

        /// @brief Discards any payloads left over from a previous connection attempt.
        void clear_client_payload_queue() noexcept;

        // Packet pump

        /// @brief Points a `msghdr` at the packet buffer so it can be reused across `recvmsg` calls.
        /// @param packet_buf The buffer receiving the datagram.
        /// @param peer_addr  Storage for the sender address.
        /// @param msg        The message header to initialise.
        /// @param iov        The single-entry scatter/gather array backing @p msg.
        static void prepare_recv_message(packet_buffer_t& packet_buf, ::sockaddr_storage& peer_addr, ::msghdr& msg,
                                         ::iovec (&iov)[1u]) noexcept;

        /// @brief Reports the first call reaching the lsquic engine from a thread that does not own it.
        /// @param operation What the caller was about to do, named in the log line.
        /// @note Reports and returns; see the thread-affinity warning on this struct for why it cannot do more.
        void check_engine_thread(std::string_view operation) noexcept;

        /// @brief Re-arms read interest on the streams parked by an exhausted payload pool.
        /// @details Runs from @ref drive_engine_once, the one pass both models tick through, so a buffer that
        ///          comes back is picked up on the next tick without the pool having to know what lsquic is.
        ///          Every parked stream is re-armed, not as many as there are free buffers: lsquic delivers
        ///          on_read one stream at a time, and a stream that finds the pool empty again simply parks
        ///          itself once more, which leaves the order they resume in lsquic's hands rather than ours.
        void resume_parked_reads() noexcept;

        /// @brief Runs one lsquic processing pass and flushes any packets it produced.
        /// @note Must run on the engine thread; see the thread-affinity warning on this struct.
        void drive_engine_once() noexcept;

        /// @brief Drives the engine a few times up front so a client's initial packets leave before the first receive.
        /// @note The iteration count is a heuristic: lsquic may need several passes to emit the full Initial flight.
        void bootstrap_initial_packets() noexcept;

        /// @brief Hands a received datagram to lsquic and drives the engine once.
        /// @param packet_buf The buffer holding the datagram.
        /// @param recv_n     The number of bytes received.
        /// @param peer_addr  The address the datagram came from.
        /// @return Success, or an error code if lsquic rejected the packet.
        [[nodiscard]] expected_void_t feed_packet_to_engine(const packet_buffer_t& packet_buf, const ssize_t recv_n,
                                                            const ::sockaddr_storage& peer_addr);

        /// @brief Receives one pending datagram and feeds it to lsquic.
        /// @param packet_buf The buffer receiving the datagram.
        /// @param msg        The message header prepared by @ref prepare_recv_message.
        /// @param peer_addr  The address the datagram came from.
        /// @return `true` when the caller should idle because the socket had nothing to read, `false` when a datagram
        ///         was processed, or an error code on failure.
        [[nodiscard]] std::expected<bool, std::error_code> receive_once(packet_buffer_t& packet_buf, ::msghdr& msg,
                                                                        const ::sockaddr_storage& peer_addr);

        /// @brief Destroys the lsquic engine, if one was created, and forgets it.
        /// @note Called by @ref base_impl before its socket is closed, so lsquic can still flush over a live descriptor.
        void destroy_lsquic_engine() noexcept;
    };

    /// @brief Common QUIC engine implementation shared between readiness and completion models.
    /// @details Adds to @ref primary_base_impl only what depends on the executor and socket types: socket ownership,
    ///          coroutine spawning, the idle-tick strategies, and the event loop.
    /// @tparam Executor  The executor type (readiness::executor or completion::executor).
    /// @tparam UdpSocket The UDP socket type (readiness::udp::socket or completion::udp::socket).
    template <typename Executor, typename UdpSocket>
    struct base_impl: primary_base_impl
    {
        /// @brief The executor driving this engine's I/O.
        Executor& exec_;
        /// @brief The bound UDP socket carrying all QUIC datagrams.
        std::unique_ptr<UdpSocket> socket_;

        /// @brief Constructs an engine bound to an executor.
        /// @param exec The executor that will drive the engine's socket and timers.
        explicit base_impl(Executor& exec) noexcept: primary_base_impl(&spawn_stream_task), exec_(exec) {}

        /// @brief Destroys the lsquic engine while the UDP socket is still open.
        ~base_impl() noexcept { destroy_lsquic_engine(); }

        /// @brief Shared initialisation logic called after model-specific socket creation.
        [[nodiscard]] expected_void_t setup(std::expected<UdpSocket, std::error_code>&& sock_res, const ip_address_t ip, const port_t port,
                                            void* ssl_ctx, const kmx::aio::quic::settings& config)
        {
            if (auto adopt_res = adopt_socket(std::move(sock_res)); !adopt_res)
                return std::unexpected(adopt_res.error());

            return setup_after_socket(ip, port, ssl_ctx, config);
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
            set_client_payload(client_payload);
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
            set_client_payloads(client_payloads);
            return connect_setup_common(std::move(sock_res), peer_ip, peer_port, hostname, ssl_ctx, config);
        }

    private:
        /// @brief The @ref primary_base_impl::spawn_stream_task_ thunk: resolves the executor and spawns the handler.
        /// @param self    The owning engine, always a @ref base_impl.
        /// @param stream  The stream the payload arrived on.
        /// @param payload The received payload.
        static void spawn_stream_task(primary_base_impl& self, ::lsquic_stream_t* const stream, stream_payload payload)
        {
            auto& impl = static_cast<base_impl&>(self);
            impl.exec_.spawn(impl.stream_handler_(stream, std::move(payload)));
        }

        /// @brief Takes ownership of a freshly created socket and publishes its descriptor to @ref socket_fd_.
        /// @param sock_res The freshly created UDP socket, or the error that creating it produced.
        /// @return Success, or the socket creation error.
        [[nodiscard]] expected_void_t adopt_socket(std::expected<UdpSocket, std::error_code>&& sock_res)
        {
            if (!sock_res)
                return std::unexpected(sock_res.error());

            socket_ = std::make_unique<UdpSocket>(std::move(*sock_res));
            socket_fd_ = socket_->get_fd();
            return {};
        }

        /// @brief Adopts the client socket and runs the non-template connect sequence.
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
            if (auto adopt_res = adopt_socket(std::move(sock_res)); !adopt_res)
                return std::unexpected(adopt_res.error());

            return connect_setup_after_socket(peer_ip, peer_port, hostname, ssl_ctx, config);
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

        /// @brief Creates and registers the readiness watchdog timer, if the executor lacks a native timeout.
        /// @param readiness_tick Receives the created timer; left empty for completion-model executors.
        /// @return Success, or an error code if the timer could not be created or registered.
        [[nodiscard]] expected_void_t setup_readiness_timer_if_needed(
            std::optional<kmx::aio::readiness::descriptor::timer>& readiness_tick);

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
        task_returning_expected_void_t wait_readiness_idle_tick(kmx::aio::readiness::descriptor::timer& readiness_tick);

    public:
        /// @brief Shared event processing loop.
        task_returning_expected_void_t process();
    };

    template <typename Executor, typename UdpSocket>
    expected_void_t base_impl<Executor, UdpSocket>::setup_readiness_timer_if_needed(
        std::optional<kmx::aio::readiness::descriptor::timer>& readiness_tick)
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

    template <typename Executor, typename UdpSocket>
    task_returning_expected_void_t base_impl<Executor, UdpSocket>::wait_readiness_idle_tick(
        kmx::aio::readiness::descriptor::timer& readiness_tick)
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

    template <typename Executor, typename UdpSocket>
    task_returning_expected_void_t base_impl<Executor, UdpSocket>::process()
    {
        running_ = true;
        packet_buffer_t packet_buf {};
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

            auto recv_res = receive_once(packet_buf, msg, peer_addr);
            if (!recv_res)
                co_return std::unexpected(recv_res.error());

            if (*recv_res)
            {
                if constexpr (requires(Executor& e) { e.async_timeout(std::uint64_t {}); })
                {
                    auto idle_res = co_await wait_completion_idle_tick();
                    if (!idle_res)
                        co_return std::unexpected(idle_res.error());
                }
                else
                {
                    auto idle_res = co_await wait_readiness_idle_tick(*readiness_tick);
                    if (!idle_res)
                        co_return std::unexpected(idle_res.error());
                }
            }
        }

        co_return expected_void_t {};
    }

} // namespace kmx::aio::quic
