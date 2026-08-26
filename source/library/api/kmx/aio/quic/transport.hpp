/// @file aio/quic/transport.hpp
/// @brief QUIC streams that model an ordered reliable byte stream.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// @note This is a separate layer from aio/quic/engine.hpp rather than a change to it. That engine delivers
///       received bytes by spawning a detached task per 4 KiB chunk, which means chunks of one stream can be
///       in flight concurrently and complete out of order - acceptable for a fire-and-forget echo sample,
///       not for anything that has to parse a byte stream. It also has no way to open a stream on demand, and
///       no way for a server to initiate one. Rather than change behaviour the existing samples depend on,
///       this provides what a protocol layer actually needs: a stream you can read from and write to, in
///       order, with backpressure, and which suspends rather than drops.
#pragma once
#if defined(KMX_AIO_FEATURE_QUIC)

    #ifndef PCH
        #include <cstddef>
        #include <cstdint>
        #include <deque>
        #include <expected>
        #include <functional>
        #include <memory>
        #include <span>
        #include <string>
        #include <system_error>
        #include <unordered_map>
        #include <cstdio>
        #include <vector>

        #include <netinet/in.h>
        #include <poll.h>
        #include <sys/socket.h>

        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/file_descriptor.hpp>
        #include <kmx/aio/task.hpp>
    #endif

extern "C"
{
    #include <lsquic.h>
}

namespace kmx::aio::quic
{
    /// @brief Everything one QUIC stream needs to behave like a byte stream.
    /// @note Held by shared_ptr because lsquic can close a stream at any point, while a coroutine may still be
    ///       suspended on it. The callback drops its reference and the awaiting side finds the stream finished
    ///       rather than a dangling pointer.
    struct stream_state
    {
        ::lsquic_stream_t* handle {};        ///< The lsquic stream, or null once it has closed.
        std::deque<char> incoming {};        ///< Bytes received and not yet read.
        std::vector<char> outgoing {};       ///< Bytes queued for writing, not yet accepted by lsquic.
        std::coroutine_handle<> reader {};   ///< Suspended reader, if any.
        std::coroutine_handle<> writer {};   ///< Suspended writer, if any.
        bool fin_received {};                ///< The peer finished its direction.
        bool closed {};                      ///< The stream is gone.
        std::error_code error {};            ///< Why it ended, if abnormally.
    };

    /// @brief Bytes buffered for one stream before reading is paused.
    /// @note Backpressure rather than a drop. The existing engine logs and discards when its pool is exhausted,
    ///       which on a reliable stream is a protocol violation the peer has no way to detect - it believes the
    ///       bytes arrived. Pausing with lsquic_stream_wantread() lets QUIC's own flow control do what it is
    ///       for: stop the sender.
    constexpr std::size_t stream_read_high_water = 256u * 1024u;

    /// @brief Shortest and longest the packet loop will sleep when nothing else wakes it.
    /// @note These bound the *timer* only. An arriving packet wakes the loop through the socket, so they no
    ///       longer set a floor on latency - they decide how long an otherwise idle connection sleeps before
    ///       servicing lsquic's own retransmits and ACKs. Before the loop could be woken by the socket, the
    ///       lower bound *was* the latency floor: at 500 us it cost a round trip about 1.5 ms, because each
    ///       direction had to wait out a tick.
    constexpr std::uint64_t min_tick_ns = 200u * 1000u;
    constexpr std::uint64_t max_tick_ns = 5u * 1000u * 1000u;

    /// @brief Teaches a server SSL_CTX to accept @p alpn.
    /// @param ssl_ctx The server context, as a void* so this header does not force BoringSSL on every consumer.
    /// @param alpn The protocol name the peer will offer.
    /// @note Needed on the server side only, and easy to miss: ea_alpn makes the *client* offer a name, but
    ///       selecting from the offer is BoringSSL's job and defaults to selecting nothing. The handshake then
    ///       fails with "no suitable application protocol" and no packet ever reaches the application, which
    ///       looks exactly like a connection that hangs.
    void configure_server_alpn(void* ssl_ctx, const char* alpn) noexcept;

    /// @brief A QUIC stream, presented as an ordered reliable byte stream.
    class stream
    {
    public:
        /// @brief Wraps @p state, which the endpoint owns.
        explicit stream(std::shared_ptr<stream_state> state, std::vector<std::coroutine_handle<>>* ready) noexcept:
            state_(std::move(state)),
            ready_(ready)
        {
        }

        stream(const stream&) = delete;
        stream& operator=(const stream&) = delete;
        stream(stream&&) noexcept = default;
        stream& operator=(stream&&) = delete;
        ~stream() noexcept = default;

        /// @brief The stream identifier, or zero once closed.
        [[nodiscard]] std::uint64_t id() const noexcept
        {
            return (state_ && state_->handle) ? static_cast<std::uint64_t>(::lsquic_stream_id(state_->handle)) : 0u;
        }

        /// @brief Whether the stream is still usable.
        [[nodiscard]] bool is_open() const noexcept { return state_ && !state_->closed; }

        /// @brief Reads whatever has arrived.
        /// @param out Destination.
        /// @return Bytes read; zero once the peer has finished and nothing is left.
        [[nodiscard]] task<std::expected<std::size_t, std::error_code>> read(const std::span<char> out) noexcept(false)
        {
            co_await readable {*state_};

            if (state_->error)
                co_return std::unexpected(state_->error);

            if (state_->incoming.empty())
                co_return std::size_t {0u}; // finished, or closed with nothing pending

            const auto count = std::min(out.size(), state_->incoming.size());
            std::copy_n(state_->incoming.begin(), count, out.begin());
            state_->incoming.erase(state_->incoming.begin(), state_->incoming.begin() + static_cast<std::ptrdiff_t>(count));

            // Room again, so let lsquic resume delivering.
            if (state_->handle && (state_->incoming.size() < stream_read_high_water))
                ::lsquic_stream_wantread(state_->handle, 1);

            co_return count;
        }

        /// @brief Writes every byte, suspending until lsquic has accepted them all.
        [[nodiscard]] task<std::expected<void, std::error_code>> write_all(const std::span<const char> in) noexcept(false)
        {
            if (state_->closed)
                co_return std::unexpected(state_->error ? state_->error : std::make_error_code(std::errc::broken_pipe));

            state_->outgoing.insert(state_->outgoing.end(), in.begin(), in.end());
            if (state_->handle)
                ::lsquic_stream_wantwrite(state_->handle, 1);

            co_await flushed {*state_};

            if (state_->error)
                co_return std::unexpected(state_->error);

            if (!state_->outgoing.empty())
                co_return std::unexpected(std::make_error_code(std::errc::broken_pipe));

            co_return std::expected<void, std::error_code> {};
        }

        /// @brief Ends this side of the stream.
        void shutdown_write() noexcept
        {
            if (state_->handle)
                ::lsquic_stream_shutdown(state_->handle, 1);
        }

    private:
        /// @brief Suspends until bytes are available, the peer finishes, or the stream fails.
        struct readable
        {
            stream_state& state;

            [[nodiscard]] bool await_ready() const noexcept
            {
                return !state.incoming.empty() || state.fin_received || state.closed || static_cast<bool>(state.error);
            }

            void await_suspend(const std::coroutine_handle<> handle) const noexcept { state.reader = handle; }
            void await_resume() const noexcept {}
        };

        /// @brief Suspends until everything queued has been handed to lsquic.
        struct flushed
        {
            stream_state& state;

            [[nodiscard]] bool await_ready() const noexcept
            {
                return state.outgoing.empty() || state.closed || static_cast<bool>(state.error);
            }

            void await_suspend(const std::coroutine_handle<> handle) const noexcept { state.writer = handle; }
            void await_resume() const noexcept {}
        };

        std::shared_ptr<stream_state> state_ {};          ///< Shared with the endpoint's callbacks.
        std::vector<std::coroutine_handle<>>* ready_ {};  ///< Where the endpoint parks woken coroutines.
    };

    /// @brief Owns an lsquic engine and its UDP socket, and drives both.
    /// @tparam Executor The model specific executor; only used for the socket I/O and the tick timer.
    ///
    /// @note One endpoint is either a client or a server, decided at setup, because lsquic's engine flags are.
    ///
    /// @note Everything here runs on one thread: lsquic is not internally synchronized, and neither are the
    ///       registries below. The packet loop is the only thing that touches them.
    template <typename Executor>
    class endpoint
    {
    public:
        /// @brief Constructs an endpoint bound to @p exec.
        explicit endpoint(Executor& exec) noexcept: exec_(&exec) {}

        /// @brief Sets the ALPN name offered on the handshake; must match the peer's.
        void set_alpn(const char* const alpn) noexcept { alpn_ = alpn; }

        endpoint(const endpoint&) = delete;
        endpoint& operator=(const endpoint&) = delete;
        endpoint(endpoint&&) = delete;
        endpoint& operator=(endpoint&&) = delete;

        /// @brief Tears the engine down.
        ~endpoint() noexcept
        {
            if (engine_ != nullptr)
                ::lsquic_engine_destroy(engine_);
        }

        /// @brief Prepares a server endpoint listening on @p ip and @p port.
        /// @param ip Address to bind.
        /// @param port Port to bind.
        /// @param ssl_ctx A configured SSL_CTX carrying the certificate chain and key.
        /// @return Nothing, or why setup failed.
        [[nodiscard]] std::expected<void, std::error_code> listen(const ip_address_t ip, const port_t port, void* ssl_ctx) noexcept
        {
            return setup(ip, port, ssl_ctx, true);
        }

        /// @brief Prepares a client endpoint and starts a connection to @p ip and @p port.
        /// @param ip Peer address.
        /// @param port Peer port.
        /// @param sni Server name to present.
        /// @param ssl_ctx A configured SSL_CTX.
        /// @return Nothing, or why setup failed.
        [[nodiscard]] std::expected<void, std::error_code> connect(const ip_address_t ip, const port_t port, const std::string& sni,
                                                                    void* ssl_ctx) noexcept
        {
            // Bind to an ephemeral local port; the peer address is where packets go.
            static constexpr std::array<std::uint8_t, 4u> any {0u, 0u, 0u, 0u};
            const auto prepared = setup(make_ip_address(any), 0u, ssl_ctx, false);
            if (!prepared)
                return prepared;

            const auto peer = make_socket_address(ip, port);
            if (!peer)
                return std::unexpected(peer.error());

            peer_ = *peer;

            ::sockaddr_storage local {};
            ::socklen_t local_len = sizeof(local);
            if (::getsockname(socket_.get(), reinterpret_cast<::sockaddr*>(&local), &local_len) != 0)
                return std::unexpected(error_from_errno());

            auto* const conn = ::lsquic_engine_connect(engine_, N_LSQVER, reinterpret_cast<const ::sockaddr*>(&local),
                                                       reinterpret_cast<const ::sockaddr*>(&peer_.storage), this, nullptr,
                                                       sni.empty() ? nullptr : sni.c_str(), 0u, nullptr, 0u, nullptr, 0u);
            if (conn == nullptr)
                return std::unexpected(std::make_error_code(std::errc::connection_refused));

            return {};
        }

        /// @brief Opens a new stream on this connection.
        /// @return The stream, or why one could not be opened.
        /// @note Either peer may open one. QUIC gives every stream its own ordering and flow control, so work
        ///       carried on separate streams cannot block work on the others - which is the whole reason to
        ///       prefer it to multiplexing everything down one.
        [[nodiscard]] task<std::expected<stream, std::error_code>> open_stream() noexcept(false)
        {
            if (failure_)
                co_return std::unexpected(failure_);

            pending_opens_ += 1u;
            if (conn_ != nullptr)
                (void) ::lsquic_conn_make_stream(conn_);

            co_await stream_opened {*this};

            if (failure_)
                co_return std::unexpected(failure_);

            if (!opened_)
                co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));

            auto state = std::move(opened_);
            opened_.reset();
            co_return stream {std::move(state), &ready_};
        }

        /// @brief Suspends until the peer opens a stream.
        /// @return The stream, or why none arrived.
        [[nodiscard]] task<std::expected<stream, std::error_code>> accept_stream() noexcept(false)
        {
            co_await stream_accepted {*this};

            if (accepted_.empty())
                co_return std::unexpected(failure_ ? failure_ : std::make_error_code(std::errc::connection_aborted));

            auto state = std::move(accepted_.front());
            accepted_.pop_front();
            co_return stream {std::move(state), &ready_};
        }

        /// @brief The first stream of the connection: opened by the client, awaited by the server.
        /// @note A convenience for the common case where one stream carries everything. Anything wanting the
        ///       independence QUIC offers should use open_stream() and accept_stream() directly.
        [[nodiscard]] task<std::expected<stream, std::error_code>> session() noexcept(false)
        {
            if (is_server_)
                return accept_stream();

            return open_stream();
        }

        /// @brief Runs the packet loop until the connection ends.
        ///
        /// @note The loop drains every packet that has arrived, lets lsquic act on them, then waits. How long
        ///       it waits comes from lsquic_engine_earliest_adv_tick(), which is when the engine next has
        ///       something to do on its own account - a retransmit, an ACK, a handshake timeout. Waiting on a
        ///       fixed interval instead would either burn CPU or delay those.
        ///
        /// @note The loop has to wake on either of two things - a packet arriving, or one of lsquic's timers
        ///       expiring - and the coroutine library offers no way to await whichever comes first. What makes
        ///       it work anyway is that io_uring's timeout is submitted with a completion count of one, so it
        ///       completes when *any* completion is posted as well as when the time runs out. Keeping a single
        ///       readability poll outstanding on the socket therefore turns the timer into a race: the poll's
        ///       completion ends the wait immediately, and the timer only bounds how long an idle connection
        ///       sleeps before servicing its own retransmits and ACKs.
        ///
        /// @note Exactly one poll is outstanding at a time, re-armed after it completes. Arming one per
        ///       iteration would leave a pending operation behind on every tick where no packet arrived, and
        ///       those accumulate until traffic happens to flush them.
        ///
        /// @note A spurious wakeup - some unrelated operation completing and ending the timeout early - is
        ///       harmless: the loop re-reads the socket, finds nothing, and sleeps again.
        [[nodiscard]] task<void> run() noexcept(false)
        {
            std::vector<char> packet(2048u);
            running_ = true;

            ::sockaddr_storage local {};
            ::socklen_t local_len = sizeof(local);
            (void) ::getsockname(socket_.get(), reinterpret_cast<::sockaddr*>(&local), &local_len);

            while (running_)
            {
                arm_readable_poll();

                for (;;)
                {
                    ::sockaddr_storage from {};
                    ::iovec iov {packet.data(), packet.size()};
                    ::msghdr msg {};
                    msg.msg_name = &from;
                    msg.msg_namelen = sizeof(from);
                    msg.msg_iov = &iov;
                    msg.msg_iovlen = 1u;

                    const auto received = ::recvmsg(socket_.get(), &msg, MSG_DONTWAIT);
                    if (received <= 0)
                        break;

                    ++packets_in_;

                    (void) ::lsquic_engine_packet_in(engine_, reinterpret_cast<const unsigned char*>(packet.data()),
                                                     static_cast<std::size_t>(received),
                                                     reinterpret_cast<const ::sockaddr*>(&local),
                                                     reinterpret_cast<const ::sockaddr*>(&from), this, 0);
                }

                ::lsquic_engine_process_conns(engine_);
                drain_ready();

                if (!running_)
                    break;

                ++ticks_;

                int diff = 0;
                const auto has_tick = ::lsquic_engine_earliest_adv_tick(engine_, &diff);
                std::uint64_t wait_ns = max_tick_ns;
                if (has_tick != 0)
                    wait_ns = (diff <= 0) ? min_tick_ns : std::min(static_cast<std::uint64_t>(diff) * 1000u, max_tick_ns);

                // Race the socket against the timer. Both signal the same wakeup and the first one wins; the
                // loser signalling later is harmless, costing at most one extra pass over an empty socket.
                exec_->spawn(tick_timer(std::max(wait_ns, min_tick_ns)));
                co_await wakeup {*this};
            }

            // Any outstanding readability poll has to be able to finish, or the executor never sees its work
            // reach zero and the process hangs on shutdown. Shutting the socket down completes it at once.
            (void) ::shutdown(socket_.get(), SHUT_RDWR);

            // Releasing every stream is part of stopping, not tidiness. A coroutine suspended on a read will
            // otherwise wait forever for a loop that has finished, and the executor will never see its work
            // reach zero - the whole process then hangs with nothing running.
            for (auto& entry: streams_)
            {
                entry.second->closed = true;
                park(ready_, entry.second->reader);
                park(ready_, entry.second->writer);
            }

            drain_ready();
            co_return;
        }

        /// @brief Suspends the packet loop until the socket or the timer wakes it.
        struct wakeup
        {
            endpoint& self;

            [[nodiscard]] bool await_ready() const noexcept { return self.wakeup_signalled_; }
            void await_suspend(const std::coroutine_handle<> handle) const noexcept { self.wakeup_waiter_ = handle; }
            void await_resume() const noexcept { self.wakeup_signalled_ = false; }
        };

        /// @brief Wakes the packet loop, from either the socket or the timer.
        /// @note Resumes directly rather than parking the handle: the loop holds nothing that a resumed
        ///       coroutine could disturb, and it is the thing that would have to do the draining anyway.
        void signal_wakeup() noexcept
        {
            wakeup_signalled_ = true;
            if (!wakeup_waiter_)
                return;

            const auto handle = wakeup_waiter_;
            wakeup_waiter_ = {};
            handle.resume();
        }

        /// @brief Sleeps for @p duration_ns, then wakes the loop.
        [[nodiscard]] task<void> tick_timer(const std::uint64_t duration_ns) noexcept(false)
        {
            (void) co_await exec_->async_timeout(duration_ns);
            signal_wakeup();
            co_return;
        }

        /// @brief Keeps one readability poll outstanding on the socket.
        void arm_readable_poll() noexcept(false)
        {
            if (poll_armed_ || !socket_.is_valid())
                return;

            poll_armed_ = true;
            exec_->spawn(readable_poll());
        }

        /// @brief Waits for the socket to become readable, then allows the next poll to be armed.
        [[nodiscard]] task<void> readable_poll() noexcept(false)
        {
            (void) co_await exec_->async_poll(socket_.get(), POLLIN);
            poll_armed_ = false;
            signal_wakeup();
            co_return;
        }

        /// @brief Stops the packet loop.
        void stop() noexcept { running_ = false; }

        /// @brief Packets received and sent, for diagnostics.
        [[nodiscard]] std::size_t packets_in() const noexcept { return packets_in_; }
        /// @brief Packets sent.
        [[nodiscard]] std::size_t packets_out() const noexcept { return packets_out_; }
        /// @brief Streams the peer has opened that nothing has accepted yet.
        [[nodiscard]] std::size_t pending_accepts() const noexcept { return accepted_.size(); }

        /// @brief The queue woken coroutines are parked on.
        [[nodiscard]] std::vector<std::coroutine_handle<>>& ready() noexcept { return ready_; }

    private:
        /// @brief Suspends until a locally opened stream exists, or the connection failed.
        struct stream_opened
        {
            endpoint& self;

            [[nodiscard]] bool await_ready() const noexcept
            {
                return static_cast<bool>(self.opened_) || static_cast<bool>(self.failure_);
            }

            void await_suspend(const std::coroutine_handle<> handle) const noexcept { self.opener_ = handle; }
            void await_resume() const noexcept {}
        };

        /// @brief Suspends until the peer has opened a stream, or the connection failed.
        struct stream_accepted
        {
            endpoint& self;

            [[nodiscard]] bool await_ready() const noexcept
            {
                return !self.accepted_.empty() || static_cast<bool>(self.failure_);
            }

            void await_suspend(const std::coroutine_handle<> handle) const noexcept { self.acceptor_ = handle; }
            void await_resume() const noexcept {}
        };

        /// @brief Creates the socket and the lsquic engine.
        [[nodiscard]] std::expected<void, std::error_code> setup(const ip_address_t ip, const port_t port, void* ssl_ctx,
                                                                  const bool server) noexcept
        {
            is_server_ = server;
            ssl_ctx_ = ssl_ctx;

            const unsigned flags = server ? LSENG_SERVER : 0u;

            // Once per process, and for both roles at once. lsquic_global_init is not a per-engine call:
            // invoking it a second time with the other role's flag reinitialises global state the first engine
            // is already relying on, and the endpoint that lost the race then silently processes nothing.
            static const bool global_ready = (::lsquic_global_init(LSQUIC_GLOBAL_CLIENT | LSQUIC_GLOBAL_SERVER) == 0);
            if (!global_ready)
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));

            auto sock = file_descriptor::create_socket(ip_family(ip), SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (!sock)
                return std::unexpected(sock.error());

            socket_ = std::move(*sock);
            const auto bound = socket_.bind(ip, port);
            if (!bound)
                return std::unexpected(bound.error());

            ::lsquic_engine_settings settings {};
            ::lsquic_engine_init_settings(&settings, flags);
            settings.es_max_streams_in = 64u;
            settings.es_idle_timeout = 30u;
            // Without this lsquic refuses to start when the versions it was built with disagree with defaults.
            char err[256] {};
            if (::lsquic_engine_check_settings(&settings, flags, err, sizeof(err)) != 0)
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));

            stream_if_.on_new_conn = &endpoint::cb_new_conn;
            stream_if_.on_conn_closed = &endpoint::cb_conn_closed;
            stream_if_.on_new_stream = &endpoint::cb_new_stream;
            stream_if_.on_read = &endpoint::cb_read;
            stream_if_.on_write = &endpoint::cb_write;
            stream_if_.on_close = &endpoint::cb_close;
            stream_if_.on_hsk_done = &endpoint::cb_hsk_done;

            ::lsquic_engine_api api {};
            api.ea_settings = &settings;
            api.ea_stream_if = &stream_if_;
            api.ea_stream_if_ctx = this;
            api.ea_packets_out = &endpoint::cb_packets_out;
            api.ea_packets_out_ctx = this;
            api.ea_get_ssl_ctx = &endpoint::cb_get_ssl_ctx;
            // QUIC requires ALPN, and both peers must offer the same name or the handshake fails with no
            // packet ever reaching the application. lsquic supplies it from here when the engine is not in
            // HTTP/3 mode, which this is not.
            api.ea_alpn = alpn_;
            if (server)
            {
                api.ea_lookup_cert = &endpoint::cb_lookup_cert;
                // ea_lookup_cert is invoked with ea_cert_lu_ctx, not with ea_stream_if_ctx. Leaving it unset
                // hands the callback a null pointer during the handshake.
                api.ea_cert_lu_ctx = this;
            }

            engine_ = ::lsquic_engine_new(flags, &api);
            if (engine_ == nullptr)
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));

            return {};
        }

        /// @brief Resumes everything the callbacks woke.
        void drain_ready() noexcept
        {
            for (std::size_t i = 0u; i != ready_.size(); ++i)
            {
                const auto handle = ready_[i];
                ready_[i] = {};
                if (handle && !handle.done())
                    handle.resume();
            }

            ready_.clear();
        }

        /// @brief Parks @p handle to be resumed by the packet loop.
        static void park(std::vector<std::coroutine_handle<>>& ready, std::coroutine_handle<>& slot) noexcept
        {
            if (!slot)
                return;

            const auto handle = slot;
            slot = {};
            ready.push_back(handle);
        }

        // lsquic callbacks

        static ::lsquic_conn_ctx_t* cb_new_conn(void* ctx, ::lsquic_conn_t* conn) noexcept
        {
            auto* const self = static_cast<endpoint*>(ctx);
            ::lsquic_conn_set_ctx(conn, reinterpret_cast<::lsquic_conn_ctx_t*>(self));
            self->conn_ = conn;
            return reinterpret_cast<::lsquic_conn_ctx_t*>(self);
        }

        static void cb_conn_closed(::lsquic_conn_t* conn) noexcept
        {
            auto* const self = reinterpret_cast<endpoint*>(::lsquic_conn_get_ctx(conn));
            if (self == nullptr)
                return;

            self->running_ = false;
            if (!self->failure_)
                self->failure_ = std::make_error_code(std::errc::connection_aborted);

            park(self->ready_, self->opener_);
            park(self->ready_, self->acceptor_);

            for (auto& entry: self->streams_)
            {
                entry.second->closed = true;
                park(self->ready_, entry.second->reader);
                park(self->ready_, entry.second->writer);
            }
        }

        static void cb_hsk_done(::lsquic_conn_t* conn, enum lsquic_hsk_status status) noexcept
        {
            auto* const self = reinterpret_cast<endpoint*>(::lsquic_conn_get_ctx(conn));
            if (self == nullptr)
                return;

            if ((status != LSQ_HSK_OK) && (status != LSQ_HSK_RESUMED_OK))
            {
                self->failure_ = std::make_error_code(std::errc::connection_refused);
                park(self->ready_, self->opener_);
                park(self->ready_, self->acceptor_);
                return;
            }

            // Streams asked for before the handshake finished could not be created yet; make them now.
            for (std::size_t i = 0u; i != self->pending_opens_; ++i)
                (void) ::lsquic_conn_make_stream(conn);
        }

        static ::lsquic_stream_ctx_t* cb_new_stream(void* ctx, ::lsquic_stream_t* handle) noexcept
        {
            auto* const self = static_cast<endpoint*>(ctx);
            if (handle == nullptr)
                return nullptr;

            auto state = std::make_shared<stream_state>();
            state->handle = handle;
            self->streams_.emplace(handle, state);

            // Which side opened it is encoded in the identifier's low bit, and it decides who receives the
            // stream: whoever called open_stream(), or whoever is waiting in accept_stream(). Getting this
            // backwards hands a peer's stream to a local opener, which then talks past whatever the peer sent.
            const auto id = static_cast<std::uint64_t>(::lsquic_stream_id(handle));
            const std::uint64_t local_initiator_bit = self->is_server_ ? 1u : 0u;
            const bool locally_opened = (id & 1u) == local_initiator_bit;

            if (locally_opened && (self->pending_opens_ != 0u))
            {
                self->pending_opens_ -= 1u;
                self->opened_ = state;
                park(self->ready_, self->opener_);
            }
            else
            {
                self->accepted_.push_back(state);
                park(self->ready_, self->acceptor_);
            }

            ::lsquic_stream_wantread(handle, 1);
            return reinterpret_cast<::lsquic_stream_ctx_t*>(state.get());
        }

        static void cb_read(::lsquic_stream_t* handle, ::lsquic_stream_ctx_t* ctx) noexcept
        {
            auto* const state = reinterpret_cast<stream_state*>(ctx);
            auto* const self = reinterpret_cast<endpoint*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(handle)));
            if ((state == nullptr) || (self == nullptr))
                return;

            char buffer[4096];
            for (;;)
            {
                const auto count = ::lsquic_stream_read(handle, buffer, sizeof(buffer));
                if (count > 0)
                {
                    state->incoming.insert(state->incoming.end(), buffer, buffer + count);
                    if (state->incoming.size() >= stream_read_high_water)
                    {
                        // Stop pulling until the reader catches up; QUIC flow control then stalls the sender.
                        ::lsquic_stream_wantread(handle, 0);
                        break;
                    }

                    continue;
                }

                if (count == 0)
                {
                    state->fin_received = true;
                    ::lsquic_stream_wantread(handle, 0);
                }

                break;
            }

            park(self->ready_, state->reader);
        }

        static void cb_write(::lsquic_stream_t* handle, ::lsquic_stream_ctx_t* ctx) noexcept
        {
            auto* const state = reinterpret_cast<stream_state*>(ctx);
            auto* const self = reinterpret_cast<endpoint*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(handle)));
            if ((state == nullptr) || (self == nullptr))
                return;

            while (!state->outgoing.empty())
            {
                const auto written = ::lsquic_stream_write(handle, state->outgoing.data(), state->outgoing.size());
                if (written <= 0)
                    break;

                state->outgoing.erase(state->outgoing.begin(), state->outgoing.begin() + written);
            }

            (void) ::lsquic_stream_flush(handle);

            if (state->outgoing.empty())
            {
                ::lsquic_stream_wantwrite(handle, 0);
                park(self->ready_, state->writer);
            }
        }

        static void cb_close(::lsquic_stream_t* handle, ::lsquic_stream_ctx_t* ctx) noexcept
        {
            auto* const state = reinterpret_cast<stream_state*>(ctx);
            auto* const self = reinterpret_cast<endpoint*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(handle)));
            if (state == nullptr)
                return;

            state->closed = true;
            state->handle = nullptr;

            if (self != nullptr)
            {
                park(self->ready_, state->reader);
                park(self->ready_, state->writer);
                self->streams_.erase(handle);
            }
        }

        static int cb_packets_out(void* ctx, const ::lsquic_out_spec* specs, unsigned count) noexcept
        {
            auto* const self = static_cast<endpoint*>(ctx);
            unsigned sent = 0u;
            for (; sent != count; ++sent)
            {
                ::msghdr msg {};
                msg.msg_name = const_cast<void*>(static_cast<const void*>(specs[sent].dest_sa));
                msg.msg_namelen = (specs[sent].dest_sa->sa_family == AF_INET) ? sizeof(::sockaddr_in) : sizeof(::sockaddr_in6);
                msg.msg_iov = specs[sent].iov;
                msg.msg_iovlen = specs[sent].iovlen;

                if (::sendmsg(self->socket_.get(), &msg, 0) < 0)
                    break;

                ++self->packets_out_;
            }

            return static_cast<int>(sent);
        }

        static struct ssl_ctx_st* cb_get_ssl_ctx(void* peer_ctx, const struct sockaddr*) noexcept
        {
            auto* const self = static_cast<endpoint*>(peer_ctx);
            return reinterpret_cast<struct ssl_ctx_st*>(self->ssl_ctx_);
        }

        static struct ssl_ctx_st* cb_lookup_cert(void* ctx, const struct sockaddr*, const char*) noexcept
        {
            auto* const self = static_cast<endpoint*>(ctx);
            return reinterpret_cast<struct ssl_ctx_st*>(self->ssl_ctx_);
        }

        Executor* exec_ {};                                                        ///< Drives the socket I/O.
        file_descriptor socket_ {};                                                ///< The UDP socket.
        ::lsquic_engine_t* engine_ {};                                             ///< The lsquic engine.
        ::lsquic_stream_if stream_if_ {};                                          ///< Callback table.
        ::lsquic_conn_t* conn_ {};                                                 ///< The connection, once established.
        void* ssl_ctx_ {};                                                         ///< Caller owned SSL_CTX.
        socket_address peer_ {};                                                   ///< Peer address, for a client.
        std::unordered_map<::lsquic_stream_t*, std::shared_ptr<stream_state>> streams_ {}; ///< Live streams.
        std::deque<std::shared_ptr<stream_state>> accepted_ {};                    ///< Streams the peer opened.
        std::shared_ptr<stream_state> opened_ {};                                  ///< Stream handed to open_stream().
        std::coroutine_handle<> opener_ {};                                        ///< Coroutine in open_stream().
        std::coroutine_handle<> acceptor_ {};                                      ///< Coroutine in accept_stream().
        std::size_t pending_opens_ {};                                             ///< open_stream() calls not yet served.
        std::vector<std::coroutine_handle<>> ready_ {};                            ///< Woken coroutines.
        std::error_code failure_ {};                                               ///< Why setup or handshake failed.
        const char* alpn_ {"kmx-rpc"};                                             ///< ALPN name offered on the handshake.
        std::size_t ticks_ {};                                                     ///< Packet loop iterations.
        std::size_t packets_in_ {};                                                ///< Packets received.
        std::size_t packets_out_ {};                                               ///< Packets sent.
        std::coroutine_handle<> wakeup_waiter_ {};                                 ///< The packet loop, when asleep.
        bool wakeup_signalled_ {};                                                 ///< A wakeup arrived before the wait.
        bool poll_armed_ {};                                                       ///< A readability poll is outstanding.
        bool is_server_ {};                                                        ///< Whether this is a server endpoint.
        bool running_ {};                                                          ///< Whether the packet loop should continue.
    };
}

#endif // KMX_AIO_FEATURE_QUIC
