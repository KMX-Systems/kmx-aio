/// @file inc/kmx/aio/quic/primary_base_impl.hpp
/// @brief Non-template core of the QUIC engine, shared verbatim by every base_impl instantiation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details This is a PRIVATE implementation detail — included only from the .cpp files.
///          It must NOT appear in any public header to avoid exposing lsquic.h to consumers.
#pragma once
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/buffer/pool.hpp>
    #include <kmx/aio/quic/base_engine.hpp>
    #include <kmx/aio/quic/detail/read_park_list.hpp>
    #include <kmx/aio/quic/engine.hpp>
    #include <kmx/aio/quic/settings.hpp>
    #include <kmx/aio/quic/stream_payload.hpp>
    #include <kmx/aio/task.hpp>

    #include <lsquic.h>

    #include <array>
    #include <atomic>
    #include <cstddef>
    #include <expected>
    #include <functional>
    #include <queue>
    #include <string>
    #include <string_view>
    #include <system_error>
    #include <thread>
    #include <unordered_set>
    #include <vector>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <sys/uio.h>
#endif

namespace kmx::aio::quic
{
    /// @brief Non-template core of the QUIC engine, shared verbatim by every @ref base_impl instantiation.
    /// @details Holds every piece of engine state that does not depend on the executor or socket types, together
    ///          with the lsquic C callbacks, the engine and socket setup steps, and the packet-pump helpers. All of
    ///          it is compiled once, in primary_base_impl.cpp, instead of once per executor/socket pair.
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

        /// @brief Points an `lsquic_stream_if` callback table at this struct's lsquic callbacks.
        /// @param stream_if The callback table to fill in.
        static void configure_stream_if(::lsquic_stream_if& stream_if) noexcept;

        /// @brief Configures lsquic callbacks, settings, and creates the lsquic_engine.
        /// @param config       The portable QUIC settings to apply.
        /// @param lsquic_flags The lsquic engine flags (`LSENG_SERVER`, `LSENG_HTTP`, ...).
        /// @return Success or an error code.
        [[nodiscard]] expected_void_t init_lsquic(const kmx::aio::quic::settings& config, unsigned lsquic_flags);

        /// @brief Binds the UDP socket and stores the local address.
        /// @return Success or an error code.
        [[nodiscard]] expected_void_t bind_socket(const ip_address_t ip, const port_t port);

        /// @brief Binds the server socket and creates the lsquic engine, once @ref socket_fd_ is known.
        /// @param params Where to bind, the borrowed `SSL_CTX` and the QUIC settings.
        /// @return Success, or an error code if the bind or engine step failed.
        [[nodiscard]] expected_void_t setup_after_socket(const start_params& params);

        /// @brief Binds an ephemeral port, creates the lsquic engine, and initiates the client connection.
        /// @param params The server to connect to, the SNI hostname, the borrowed `SSL_CTX` and the QUIC settings;
        ///               the payloads are not read here.
        /// @return Success, or an error code if any setup step failed.
        [[nodiscard]] expected_void_t connect_setup_after_socket(const connect_params& params);

        /// @brief Connects the socket to one peer and records the local address the kernel chose.
        /// @param peer The peer to connect to.
        /// @return Success, or the reason the socket could not be connected.
        [[nodiscard]] expected_void_t connect_socket_to(const socket_address& peer);

        /// @brief Serves a stream opened for the post-handshake bootstrap, if this is one.
        /// @param stream The stream being asked for data.
        /// @return `true` when the stream was one of those and has now been handled.
        [[nodiscard]] bool write_post_handshake_stream(::lsquic_stream_t* stream) noexcept;

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
}
