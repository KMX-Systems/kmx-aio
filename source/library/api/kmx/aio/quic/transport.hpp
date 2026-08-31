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
        #include <utility>
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
    /// @brief A byte queue that is filled at one end and drained from the other.
    ///
    /// @note Both directions of a stream are pure FIFOs, and the obvious way to write one - append to a
    ///       container, erase what has been taken from its front - moves every byte still queued on every
    ///       read. A protocol that reads a short header before its payload pays that for the whole payload,
    ///       and a stream read in small pieces costs a quadratic in what passes through it.
    ///
    /// @note Instead nothing moves when bytes are taken; a cursor advances. The storage behind the cursor is
    ///       reclaimed only once it accounts for half the buffer, so a compaction never moves more bytes than
    ///       have already been consumed since the last one - which makes its amortised cost a constant per
    ///       byte rather than a factor on the queue's length.
    class byte_buffer
    {
    public:
        /// @brief Whether nothing is queued.
        [[nodiscard]] bool empty() const noexcept { return read_pos_ == data_.size(); }

        /// @brief How many bytes are queued and not yet taken.
        [[nodiscard]] std::size_t size() const noexcept { return data_.size() - read_pos_; }

        /// @brief The queued bytes, contiguously; valid until the next append() or consume().
        [[nodiscard]] const char* data() const noexcept { return data_.data() + read_pos_; }

        /// @brief Queues @p count bytes read from @p first.
        void append(const char* const first, const std::size_t count) noexcept(false)
        {
            data_.insert(data_.end(), first, first + count);
        }

        /// @brief Drops the first @p count queued bytes, which must not exceed size().
        void consume(std::size_t count) noexcept;

    private:
        std::vector<char> data_ {};  ///< Queued bytes, preceded by those already taken.
        std::size_t read_pos_ {};    ///< How much of @ref data_ has been taken.
    };

    /// @brief Everything one QUIC stream needs to behave like a byte stream.
    /// @note Held by shared_ptr because lsquic can close a stream at any point, while a coroutine may still be
    ///       suspended on it. The callback drops its reference and the awaiting side finds the stream finished
    ///       rather than a dangling pointer.
    struct stream_state
    {
        ::lsquic_stream_t* handle {};        ///< The lsquic stream, or null once it has closed.
        ::lsquic_conn_t* conn {};            ///< The connection it belongs to; kept after @ref handle is cleared.
        byte_buffer incoming {};             ///< Bytes received and not yet read.
        byte_buffer outgoing {};             ///< Bytes queued for writing, not yet accepted by lsquic.
        coroutine_handle_t reader {};   ///< Suspended reader, if any.
        coroutine_handle_t writer {};   ///< Suspended writer, if any.
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
        explicit stream(std::shared_ptr<stream_state> state) noexcept: state_(std::move(state)) {}

        stream(const stream&) = delete;
        stream& operator=(const stream&) = delete;
        stream(stream&&) noexcept = default;
        stream& operator=(stream&&) = delete;
        ~stream() noexcept = default;

        /// @brief The stream identifier, or zero once closed.
        [[nodiscard]] std::uint64_t id() const noexcept;

        /// @brief Whether the stream is still usable.
        [[nodiscard]] bool is_open() const noexcept { return state_ && !state_->closed; }

        /// @brief Reads whatever has arrived.
        /// @param out Destination.
        /// @return Bytes read; zero once the peer has finished and nothing is left.
        [[nodiscard]] task_returning_expected_size_t read(std::span<char> out) noexcept(false);

        /// @brief Writes every byte, suspending until lsquic has accepted them all.
        [[nodiscard]] task_returning_expected_void_t write_all(std::span<const char> in) noexcept(false);

        /// @brief Ends this side of the stream.
        void shutdown_write() noexcept;

    private:
        /// @brief Shared coroutine mechanics for stream waiters.
        template <coroutine_handle_t stream_state::* waiter>
        struct awaiter_base
        {
            stream_state& state;

            void await_suspend(const coroutine_handle_t handle) const noexcept { state.*waiter = handle; }
            void await_resume() const noexcept {}
        };

        /// @brief Suspends until bytes are available, the peer finishes, or the stream fails.
        struct readable: awaiter_base<&stream_state::reader>
        {
            [[nodiscard]] bool await_ready() const noexcept
            {
                return !state.incoming.empty() || state.fin_received || state.closed || static_cast<bool>(state.error);
            }
        };

        /// @brief Suspends until everything queued has been handed to lsquic.
        struct flushed: awaiter_base<&stream_state::writer>
        {
            [[nodiscard]] bool await_ready() const noexcept
            {
                return state.outgoing.empty() || state.closed || static_cast<bool>(state.error);
            }
        };

        std::shared_ptr<stream_state> state_ {};  ///< Shared with the endpoint's callbacks.
    };

    /// @brief Owns an lsquic engine and its UDP socket, and drives both.
    ///
    /// @note All of the behaviour is here, and none of it is a template. The executor appears only as the
    ///       three operations the packet loop actually needs - spawn a task, sleep, wait for the socket -
    ///       which @ref endpoint supplies. That keeps the lsquic callbacks, the engine setup and the loop
    ///       itself in one translation unit instead of in the header, and it means the loop can be read as
    ///       the single sequence it is rather than as hooks a derived class calls in the right order.
    ///
    /// @note One endpoint is either a client or a server, decided at setup, because lsquic's engine flags are.
    ///
    /// @note Everything here runs on one thread: lsquic is not internally synchronized, and neither are the
    ///       registries below. The packet loop is the only thing that touches them.
    ///
    /// @note Not a polymorphic base. The three virtuals below exist to reach the executor, the destructor is
    ///       protected so an endpoint is never destroyed through a pointer to this, and nothing else here is
    ///       virtual - the loop calls them once per iteration, against a socket syscall on the same pass.
    class basic_endpoint
    {
    public:
        basic_endpoint(const basic_endpoint&) = delete;
        basic_endpoint& operator=(const basic_endpoint&) = delete;
        basic_endpoint(basic_endpoint&&) = delete;
        basic_endpoint& operator=(basic_endpoint&&) = delete;

        /// @brief Sets the ALPN name offered on the handshake; must match the peer's.
        void set_alpn(const char* const alpn) noexcept { alpn_ = alpn; }

        /// @brief Prepares a server endpoint listening on @p ip and @p port.
        /// @param ip Address to bind.
        /// @param port Port to bind.
        /// @param ssl_ctx A configured SSL_CTX carrying the certificate chain and key.
        /// @return Nothing, or why setup failed.
        [[nodiscard]] expected_void_t listen(const ip_address_t ip, const port_t port, void* ssl_ctx) noexcept
        {
            return setup(ip, port, ssl_ctx, true);
        }

        /// @brief Prepares a client endpoint and starts a connection to @p ip and @p port.
        /// @param ip Peer address.
        /// @param port Peer port.
        /// @param sni Server name to present.
        /// @param ssl_ctx A configured SSL_CTX.
        /// @return Nothing, or why setup failed.
        [[nodiscard]] expected_void_t connect(const ip_address_t ip, const port_t port, const std::string& sni,
                                                                   void* ssl_ctx) noexcept;

        /// @brief Opens a new stream on this connection.
        /// @return The stream, or why one could not be opened.
        /// @note Either peer may open one. QUIC gives every stream its own ordering and flow control, so work
        ///       carried on separate streams cannot block work on the others - which is the whole reason to
        ///       prefer it to multiplexing everything down one.
        /// @note On a server this opens on the most recently accepted connection, which is only meaningful
        ///       while it serves one peer at a time; with several connected there is no way here to say which
        ///       one is meant. Before any connection has been accepted it fails rather than suspending: a
        ///       client's request can wait for its handshake because one is under way, and a server's cannot
        ///       wait for a peer that may never arrive.
        [[nodiscard]] task<std::expected<stream, std::error_code>> open_stream() noexcept(false);

        /// @brief Suspends until the peer opens a stream.
        /// @return The stream, or why none arrived.
        [[nodiscard]] task<std::expected<stream, std::error_code>> accept_stream() noexcept(false);

        /// @brief The first stream of the connection: opened by the client, awaited by the server.
        /// @note A convenience for the common case where one stream carries everything. Anything wanting the
        ///       independence QUIC offers should use open_stream() and accept_stream() directly.
        [[nodiscard]] task<std::expected<stream, std::error_code>> session() noexcept(false);

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
        [[nodiscard]] task<void> run() noexcept(false);

        /// @brief Closes the connection, telling the peer it has gone.
        /// @note Distinct from stop(), which only ends the local packet loop. Ending a connection by stopping
        ///       the loop tells the peer nothing at all: it holds the connection, and everything it had for
        ///       it, until the idle timeout expires half a minute later. lsquic emits the CONNECTION_CLOSE on
        ///       the next pass of the loop, so the loop has to keep running for a moment after this returns.
        void close() noexcept
        {
            if (conn_ != nullptr)
                ::lsquic_conn_close(conn_);
        }

        /// @brief Stops the packet loop.
        void stop() noexcept { running_ = false; }

        /// @brief Whether the packet loop is still running.
        /// @note What tells a server's accept loop apart from the endpoint shutting down: `accept_stream()`
        ///       refuses in both cases, and only one of them is the end.
        [[nodiscard]] bool is_running() const noexcept { return running_; }

        /// @brief Whether a connection is currently established.
        [[nodiscard]] bool is_connected() const noexcept { return conn_ != nullptr; }

        /// @brief The port the socket is bound to; with port 0 that is whichever one the kernel picked.
        [[nodiscard]] port_t local_port() const noexcept;

        /// @brief Packets received and sent, for diagnostics.
        [[nodiscard]] std::size_t packets_in() const noexcept { return packets_in_; }
        /// @brief Packets sent.
        [[nodiscard]] std::size_t packets_out() const noexcept { return packets_out_; }
        /// @brief Streams the peer has opened that nothing has accepted yet.
        [[nodiscard]] std::size_t pending_accepts() const noexcept { return accepted_.size(); }

        /// @brief The queue woken coroutines are parked on.
        [[nodiscard]] std::vector<coroutine_handle_t>& ready() noexcept { return ready_; }

    protected:
        basic_endpoint() noexcept = default;

        /// @brief Tears the engine down.
        /// @note Protected and non-virtual: this is a base for reuse, not for polymorphism, so an endpoint is
        ///       never destroyed through a pointer to it.
        ~basic_endpoint() noexcept
        {
            if (engine_ != nullptr)
                ::lsquic_engine_destroy(engine_);
        }

        /// @brief Submits @p t to the executor as a top-level task.
        /// @warning A lambda coroutine does not own its closure: the closure object is a temporary
        ///          destroyed at the end of the full-expression, while the coroutine frame keeps a
        ///          pointer into it. Spawning one directly - spawn([&]() -> task<void> { ... }()) -
        ///          therefore leaves every capture dangling from the first suspension onwards. Give
        ///          the lambda a name that outlives the run, or spawn a coroutine function instead,
        ///          whose parameters are copied into the frame:
        ///          @code
        ///          auto body = [&]() -> task<void> { ... };   // outlives exec.run()
        ///          exec.spawn(body());
        ///          @endcode
        virtual void io_spawn(task<void>&& t) noexcept(false) = 0;

        /// @brief Sleeps for @p duration_ns on the executor.
        [[nodiscard]] virtual task_returning_expected_void_t io_timeout(std::uint64_t duration_ns) noexcept(false) = 0;

        /// @brief Waits on the executor for @p poll_mask on @p fd.
        [[nodiscard]] virtual task<expected_int_t> io_poll(fd_t fd, unsigned poll_mask) noexcept(false) = 0;

    private:
        /// @brief Suspends the packet loop until the socket or the timer wakes it.
        struct wakeup
        {
            basic_endpoint& self;

            [[nodiscard]] bool await_ready() const noexcept { return self.wakeup_signalled_; }
            void await_suspend(const coroutine_handle_t handle) const noexcept { self.wakeup_waiter_ = handle; }
            void await_resume() const noexcept { self.wakeup_signalled_ = false; }
        };

        /// @brief Suspends until a locally opened stream exists, or the connection failed.
        struct stream_opened
        {
            basic_endpoint& self;

            [[nodiscard]] bool await_ready() const noexcept
            {
                return static_cast<bool>(self.opened_) || static_cast<bool>(self.failure_);
            }

            void await_suspend(const coroutine_handle_t handle) const noexcept { self.opener_ = handle; }
            void await_resume() const noexcept {}
        };

        /// @brief Suspends until the peer has opened a stream, or the connection failed.
        struct stream_accepted
        {
            basic_endpoint& self;

            [[nodiscard]] bool await_ready() const noexcept
            {
                return !self.accepted_.empty() || static_cast<bool>(self.failure_);
            }

            void await_suspend(const coroutine_handle_t handle) const noexcept { self.acceptor_ = handle; }
            void await_resume() const noexcept {}
        };

        /// @brief Creates the socket and the lsquic engine.
        [[nodiscard]] expected_void_t setup(const ip_address_t ip, const port_t port, void* ssl_ctx,
                                                                 const bool server) noexcept;

        /// @brief Wakes the packet loop, from either the socket or the timer.
        /// @note Resumes directly rather than parking the handle: the loop holds nothing that a resumed
        ///       coroutine could disturb, and it is the thing that would have to do the draining anyway.
        void signal_wakeup() noexcept;

        /// @brief Sleeps for @p duration_ns, then wakes the loop.
        [[nodiscard]] task<void> tick_timer(std::uint64_t duration_ns) noexcept(false);

        /// @brief Keeps one readability poll outstanding on the socket.
        void arm_readable_poll() noexcept(false);

        /// @brief Waits for the socket to become readable, then allows the next poll to be armed.
        [[nodiscard]] task<void> readable_poll() noexcept(false);

        /// @brief Resumes everything the callbacks woke.
        void drain_ready() noexcept;

        /// @brief Parks @p slot to be resumed by the packet loop.
        /// @note Every caller must first establish whatever the parked coroutine's awaiter tests for. A
        ///       coroutine resumed without that cannot tell it has been woken for nothing, and one waiting on
        ///       a read reports the end of its stream when it finds no bytes.
        static void park(std::vector<coroutine_handle_t>& ready, coroutine_handle_t& slot) noexcept;

        // lsquic callbacks

        static ::lsquic_conn_ctx_t* cb_new_conn(void* ctx, ::lsquic_conn_t* conn) noexcept;

        /// @brief One connection has gone.
        ///
        /// @note **A CLIENT ENDPOINT *IS* ITS CONNECTION; A SERVER ENDPOINT OUTLIVES EVERY CONNECTION IT
        ///       ACCEPTS.** This used to make no distinction, and three things followed from that on a server.
        ///
        ///       Clearing @ref running_ stopped the packet loop - `run()` is `while (running_)` - so the
        ///       socket stopped being read and `accept_stream()` failed for ever after. The first client to
        ///       hang up made the server deaf to everybody, permanently, and from the outside it looked like
        ///       a server that worked exactly once: the datagrams piled up unread in the receive queue and
        ///       nothing answered.
        ///
        ///       Walking @ref streams_ unconditionally was the second. That map is keyed by stream and holds
        ///       every stream on the endpoint rather than the closing connection's, so one client going away
        ///       marked *other* clients' streams closed and woke their readers with an end nobody had
        ///       reached. It is filtered by connection in the body, which for a client changes nothing - all
        ///       of its streams are on its one connection - and on a server tears down exactly what died.
        ///
        ///       The third only appears once the endpoint survives: what the connection left behind outlives
        ///       it. @ref conn_ would point at memory lsquic is about to free, and the next `open_stream()`
        ///       would hand that to `lsquic_conn_make_stream()` - the client is spared only because
        ///       @ref failure_ makes it return before it gets there. A request counted in
        ///       @ref pending_opens_ would be served by the *next* connection's first stream, which then goes
        ///       to nobody, and a stream left in @ref opened_ would be handed to whoever opens next as though
        ///       they had just opened it. Both are dropped there.
        ///
        ///       @ref accepted_ is deliberately not: a stream the peer opened and the application has not
        ///       taken yet still holds whatever arrived on it, and bytes that reached us before the close are
        ///       bytes the peer is entitled to consider delivered. It comes out of `accept_stream()` closed,
        ///       reads out what it buffered, and then ends.
        ///
        ///       The opener and the acceptor are woken either way. On a client there will never be another
        ///       stream, so the wake-up is how they learn that; on a server it is spurious - the caller finds
        ///       nothing accepted, is handed `connection_aborted` and waits again - so an accept loop must
        ///       treat a refusal as transient rather than as the end of the server.
        static void cb_conn_closed(::lsquic_conn_t* conn) noexcept;

        static void cb_hsk_done(::lsquic_conn_t* conn, enum lsquic_hsk_status status) noexcept;
        static ::lsquic_stream_ctx_t* cb_new_stream(void* ctx, ::lsquic_stream_t* handle) noexcept;
        static void cb_read(::lsquic_stream_t* handle, ::lsquic_stream_ctx_t* ctx) noexcept;
        static void cb_write(::lsquic_stream_t* handle, ::lsquic_stream_ctx_t* ctx) noexcept;
        static void cb_close(::lsquic_stream_t* handle, ::lsquic_stream_ctx_t* ctx) noexcept;
        static int cb_packets_out(void* ctx, const ::lsquic_out_spec* specs, unsigned count) noexcept;
        static struct ssl_ctx_st* cb_get_ssl_ctx(void* peer_ctx, const struct sockaddr*) noexcept;
        static struct ssl_ctx_st* cb_lookup_cert(void* ctx, const struct sockaddr*, const char*) noexcept;

        file_descriptor socket_ {};                                                ///< The UDP socket.
        ::lsquic_engine_t* engine_ {};                                             ///< The lsquic engine.
        ::lsquic_stream_if stream_if_ {};                                          ///< Callback table.
        ::lsquic_conn_t* conn_ {};                                                 ///< The connection, once established.
        void* ssl_ctx_ {};                                                         ///< Caller owned SSL_CTX.
        socket_address peer_ {};                                                   ///< Peer address, for a client.
        std::unordered_map<::lsquic_stream_t*, std::shared_ptr<stream_state>> streams_ {}; ///< Live streams.
        std::deque<std::shared_ptr<stream_state>> accepted_ {};                    ///< Streams the peer opened.
        std::shared_ptr<stream_state> opened_ {};                                  ///< Stream handed to open_stream().
        coroutine_handle_t opener_ {};                                        ///< Coroutine in open_stream().
        coroutine_handle_t acceptor_ {};                                      ///< Coroutine in accept_stream().
        std::size_t pending_opens_ {};                                             ///< open_stream() calls not yet served.
        std::vector<coroutine_handle_t> ready_ {};                            ///< Woken coroutines.
        std::error_code failure_ {};                                               ///< Why setup or handshake failed.
        const char* alpn_ {"kmx-rpc"};                                             ///< ALPN name offered on the handshake.
        std::size_t ticks_ {};                                                     ///< Packet loop iterations.
        std::size_t packets_in_ {};                                                ///< Packets received.
        std::size_t packets_out_ {};                                               ///< Packets sent.
        coroutine_handle_t wakeup_waiter_ {};                                 ///< The packet loop, when asleep.
        bool wakeup_signalled_ {};                                                 ///< A wakeup arrived before the wait.
        bool poll_armed_ {};                                                       ///< A readability poll is outstanding.
        bool is_server_ {};                                                        ///< Whether this is a server endpoint.
        bool running_ {};                                                          ///< Whether the packet loop should continue.
    };

    /// @brief A @ref basic_endpoint driven by @p Executor.
    /// @tparam Executor The model specific executor; only used for the socket I/O and the tick timer.
    ///
    /// @note The whole of the template: it binds the three operations the packet loop needs to one executor's
    ///       spelling of them. Everything else is compiled once, in basic_endpoint.
    template <typename Executor>
    class endpoint final: public basic_endpoint
    {
    public:
        /// @brief Constructs an endpoint bound to @p exec.
        explicit endpoint(Executor& exec) noexcept: exec_(&exec) {}

    private:
        void io_spawn(task<void>&& t) noexcept(false) override { exec_->spawn(std::move(t)); }

        [[nodiscard]] task_returning_expected_void_t io_timeout(const std::uint64_t duration_ns) noexcept(false) override
        {
            return exec_->async_timeout(duration_ns);
        }

        [[nodiscard]] task<expected_int_t> io_poll(const fd_t fd, const unsigned poll_mask) noexcept(false) override
        {
            return exec_->async_poll(fd, poll_mask);
        }

        Executor* exec_ {}; ///< Drives the socket I/O.
    };
}

#endif // KMX_AIO_FEATURE_QUIC
