/// @file aio/quic/transport.cpp
/// @brief Stream and endpoint bodies, plus the ALPN wiring, for the QUIC transport.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#if defined(KMX_AIO_FEATURE_QUIC)

    #include <algorithm>
    #include <array>
    #include <cstring>
    #include <kmx/aio/quic/transport.hpp>
    #include <openssl/ssl.h>

namespace kmx::aio::quic
{
    namespace detail
    {
        /// @brief The name a server accepts; one per process, which is all this layer needs.
        static std::string server_alpn;

        /// @brief Selects the configured name out of the client's offer.
        static int select_alpn(::SSL*, const unsigned char** out, unsigned char* out_len, const unsigned char* in,
                               unsigned int in_len, void*) noexcept
        {
            // The offer is a sequence of length-prefixed names; walk it looking for the one we speak.
            for (unsigned int i = 0u; i < in_len;)
            {
                const unsigned int length = in[i];
                if ((length == 0u) || ((i + 1u + length) > in_len))
                    break;

                if ((length == server_alpn.size()) && (std::memcmp(&in[i + 1u], server_alpn.data(), length) == 0))
                {
                    *out = &in[i + 1u];
                    *out_len = static_cast<unsigned char>(length);
                    return SSL_TLSEXT_ERR_OK;
                }

                i += 1u + length;
            }

            return SSL_TLSEXT_ERR_ALERT_FATAL;
        }
    }

    void byte_buffer::consume(const std::size_t count) noexcept
    {
        read_pos_ += count;
        if (read_pos_ == data_.size())
        {
            // Everything taken, so start again at the front rather than compacting. The capacity stays,
            // which is what makes a stream read to exhaustion and refilled cost no allocation at all.
            data_.clear();
            read_pos_ = 0u;
        }
        else if ((read_pos_ * 2u) >= data_.size())
        {
            data_.erase(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(read_pos_));
            read_pos_ = 0u;
        }
    }

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

    task_returning_expected_void_t stream::write_all(const std::span<const char> in) noexcept(false)
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

    expected_void_t basic_endpoint::connect(const ip_address_t ip, const port_t port, const std::string& sni,
                                                                 void* const ssl_ctx) noexcept
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

    task<std::expected<stream, std::error_code>> basic_endpoint::open_stream() noexcept(false)
    {
        if (failure_)
            co_return std::unexpected(failure_);

        if (is_server_ && (conn_ == nullptr))
            co_return std::unexpected(std::make_error_code(std::errc::not_connected));

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
        co_return stream {std::move(state)};
    }

    task<std::expected<stream, std::error_code>> basic_endpoint::accept_stream() noexcept(false)
    {
        co_await stream_accepted {*this};

        if (accepted_.empty())
            co_return std::unexpected(failure_ ? failure_ : std::make_error_code(std::errc::connection_aborted));

        auto state = std::move(accepted_.front());
        accepted_.pop_front();
        co_return stream {std::move(state)};
    }

    task<std::expected<stream, std::error_code>> basic_endpoint::session() noexcept(false)
    {
        if (is_server_)
            return accept_stream();

        return open_stream();
    }

    task<void> basic_endpoint::run() noexcept(false)
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
                                                 static_cast<std::size_t>(received), reinterpret_cast<const ::sockaddr*>(&local),
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
            io_spawn(tick_timer(std::max(wait_ns, min_tick_ns)));
            co_await wakeup {*this};
        }

        // Any outstanding readability poll has to be able to finish, or the executor never sees its work
        // reach zero and the process hangs on shutdown. Shutting the socket down completes it at once.
        (void) ::shutdown(socket_.get(), SHUT_RDWR);

        // The same goes for anyone waiting on a stream that will now never arrive. On a client
        // cb_conn_closed had already woken them; on a server it deliberately no longer ends the endpoint,
        // so stopping the loop is the only thing left that can, and an accept loop suspended here would
        // otherwise keep the executor's work count off zero for ever.
        if (!failure_)
            failure_ = std::make_error_code(std::errc::connection_aborted);

        park(ready_, opener_);
        park(ready_, acceptor_);

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

    void basic_endpoint::signal_wakeup() noexcept
    {
        wakeup_signalled_ = true;
        if (!wakeup_waiter_)
            return;

        const auto handle = wakeup_waiter_;
        wakeup_waiter_ = {};
        handle.resume();
    }

    task<void> basic_endpoint::tick_timer(const std::uint64_t duration_ns) noexcept(false)
    {
        (void) co_await io_timeout(duration_ns);
        signal_wakeup();
        co_return;
    }

    void basic_endpoint::arm_readable_poll() noexcept(false)
    {
        if (poll_armed_ || !socket_.is_valid())
            return;

        poll_armed_ = true;
        io_spawn(readable_poll());
    }

    task<void> basic_endpoint::readable_poll() noexcept(false)
    {
        (void) co_await io_poll(socket_.get(), POLLIN);
        poll_armed_ = false;
        signal_wakeup();
        co_return;
    }

    port_t basic_endpoint::local_port() const noexcept
    {
        ::sockaddr_storage addr {};
        ::socklen_t length = sizeof(addr);
        if (::getsockname(socket_.get(), reinterpret_cast<::sockaddr*>(&addr), &length) != 0)
            return 0u;

        if (addr.ss_family == AF_INET6)
            return ::ntohs(reinterpret_cast<const ::sockaddr_in6*>(&addr)->sin6_port);

        return ::ntohs(reinterpret_cast<const ::sockaddr_in*>(&addr)->sin_port);
    }

    expected_void_t basic_endpoint::setup(const ip_address_t ip, const port_t port, void* const ssl_ctx,
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

        stream_if_.on_new_conn = &basic_endpoint::cb_new_conn;
        stream_if_.on_conn_closed = &basic_endpoint::cb_conn_closed;
        stream_if_.on_new_stream = &basic_endpoint::cb_new_stream;
        stream_if_.on_read = &basic_endpoint::cb_read;
        stream_if_.on_write = &basic_endpoint::cb_write;
        stream_if_.on_close = &basic_endpoint::cb_close;
        stream_if_.on_hsk_done = &basic_endpoint::cb_hsk_done;

        ::lsquic_engine_api api {};
        api.ea_settings = &settings;
        api.ea_stream_if = &stream_if_;
        api.ea_stream_if_ctx = this;
        api.ea_packets_out = &basic_endpoint::cb_packets_out;
        api.ea_packets_out_ctx = this;
        api.ea_get_ssl_ctx = &basic_endpoint::cb_get_ssl_ctx;
        // QUIC requires ALPN, and both peers must offer the same name or the handshake fails with no
        // packet ever reaching the application. lsquic supplies it from here when the engine is not in
        // HTTP/3 mode, which this is not.
        api.ea_alpn = alpn_;
        if (server)
        {
            api.ea_lookup_cert = &basic_endpoint::cb_lookup_cert;
            // ea_lookup_cert is invoked with ea_cert_lu_ctx, not with ea_stream_if_ctx. Leaving it unset
            // hands the callback a null pointer during the handshake.
            api.ea_cert_lu_ctx = this;
        }

        engine_ = ::lsquic_engine_new(flags, &api);
        if (engine_ == nullptr)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        return {};
    }

    void basic_endpoint::drain_ready() noexcept
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

    void basic_endpoint::park(std::vector<coroutine_handle_t>& ready, coroutine_handle_t& slot) noexcept
    {
        if (!slot)
            return;

        const auto handle = slot;
        slot = {};
        ready.push_back(handle);
    }

    ::lsquic_conn_ctx_t* basic_endpoint::cb_new_conn(void* const ctx, ::lsquic_conn_t* const conn) noexcept
    {
        auto* const self = static_cast<basic_endpoint*>(ctx);
        ::lsquic_conn_set_ctx(conn, reinterpret_cast<::lsquic_conn_ctx_t*>(self));
        self->conn_ = conn;
        return reinterpret_cast<::lsquic_conn_ctx_t*>(self);
    }

    void basic_endpoint::cb_conn_closed(::lsquic_conn_t* const conn) noexcept
    {
        auto* const self = reinterpret_cast<basic_endpoint*>(::lsquic_conn_get_ctx(conn));
        if (self == nullptr)
            return;

        if (!self->is_server_)
        {
            self->running_ = false;
            if (!self->failure_)
                self->failure_ = std::make_error_code(std::errc::connection_aborted);
        }

        if (self->conn_ == conn)
        {
            self->conn_ = nullptr;

            // Opens issued against this connection can never be served now. Counting them per connection
            // would be more precise, but open_stream() only ever targets conn_, so this is all of them.
            self->pending_opens_ = 0u;
        }

        // A stream nobody ever took, on a connection that is now gone. Left in place it would be handed
        // to the next open_stream() as the stream it just asked for, on a connection it never named.
        if (self->opened_ && (self->opened_->conn == conn))
            self->opened_.reset();

        park(self->ready_, self->opener_);
        park(self->ready_, self->acceptor_);

        for (auto& entry: self->streams_)
        {
            // Only this connection's streams are this callback's business. The connection is recorded on
            // the state rather than read back with lsquic_stream_conn(), which needs a handle cb_close may
            // already have cleared.
            if (entry.second->conn != conn)
                continue;

            entry.second->closed = true;
            park(self->ready_, entry.second->reader);
            park(self->ready_, entry.second->writer);
        }
    }

    void basic_endpoint::cb_hsk_done(::lsquic_conn_t* const conn, const enum lsquic_hsk_status status) noexcept
    {
        auto* const self = reinterpret_cast<basic_endpoint*>(::lsquic_conn_get_ctx(conn));
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

    ::lsquic_stream_ctx_t* basic_endpoint::cb_new_stream(void* const ctx, ::lsquic_stream_t* const handle) noexcept
    {
        auto* const self = static_cast<basic_endpoint*>(ctx);
        if (handle == nullptr)
            return nullptr;

        auto state = std::make_shared<stream_state>();
        state->handle = handle;
        state->conn = ::lsquic_stream_conn(handle);
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

    void basic_endpoint::cb_read(::lsquic_stream_t* const handle, ::lsquic_stream_ctx_t* const ctx) noexcept
    {
        auto* const state = reinterpret_cast<stream_state*>(ctx);
        auto* const self = reinterpret_cast<basic_endpoint*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(handle)));
        if ((state == nullptr) || (self == nullptr))
            return;

        char buffer[4096];
        for (;;)
        {
            const auto count = ::lsquic_stream_read(handle, buffer, sizeof(buffer));
            if (count > 0)
            {
                state->incoming.append(buffer, static_cast<std::size_t>(count));
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

        // Only once there is something for the reader to find. lsquic calls this whenever the stream is
        // readable, which includes a call that reads nothing at all - it can be woken by a packet carrying
        // only an ACK, or find the data already drained by the previous call. A reader resumed with an empty
        // buffer and no FIN cannot tell that apart from the end of the stream: read() reports zero bytes,
        // which is exactly what it reports at the end, and the caller stops reading with the rest of the
        // message still to come.
        if (!state->incoming.empty() || state->fin_received)
            park(self->ready_, state->reader);
    }

    void basic_endpoint::cb_write(::lsquic_stream_t* const handle, ::lsquic_stream_ctx_t* const ctx) noexcept
    {
        auto* const state = reinterpret_cast<stream_state*>(ctx);
        auto* const self = reinterpret_cast<basic_endpoint*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(handle)));
        if ((state == nullptr) || (self == nullptr))
            return;

        while (!state->outgoing.empty())
        {
            const auto written = ::lsquic_stream_write(handle, state->outgoing.data(), state->outgoing.size());
            if (written <= 0)
                break;

            state->outgoing.consume(static_cast<std::size_t>(written));
        }

        (void) ::lsquic_stream_flush(handle);

        if (state->outgoing.empty())
        {
            ::lsquic_stream_wantwrite(handle, 0);
            park(self->ready_, state->writer);
        }
    }

    void basic_endpoint::cb_close(::lsquic_stream_t* const handle, ::lsquic_stream_ctx_t* const ctx) noexcept
    {
        auto* const state = reinterpret_cast<stream_state*>(ctx);
        auto* const self = reinterpret_cast<basic_endpoint*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(handle)));
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

    int basic_endpoint::cb_packets_out(void* const ctx, const ::lsquic_out_spec* const specs, const unsigned count) noexcept
    {
        auto* const self = static_cast<basic_endpoint*>(ctx);
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

    struct ssl_ctx_st* basic_endpoint::cb_get_ssl_ctx(void* const peer_ctx, const struct sockaddr*) noexcept
    {
        auto* const self = static_cast<basic_endpoint*>(peer_ctx);
        return reinterpret_cast<struct ssl_ctx_st*>(self->ssl_ctx_);
    }

    struct ssl_ctx_st* basic_endpoint::cb_lookup_cert(void* const ctx, const struct sockaddr*, const char*) noexcept
    {
        auto* const self = static_cast<basic_endpoint*>(ctx);
        return reinterpret_cast<struct ssl_ctx_st*>(self->ssl_ctx_);
    }

    void configure_server_alpn(void* const ssl_ctx, const char* const alpn) noexcept
    {
        if ((ssl_ctx == nullptr) || (alpn == nullptr))
            return;

        detail::server_alpn = alpn;
        ::SSL_CTX_set_alpn_select_cb(static_cast<::SSL_CTX*>(ssl_ctx), &detail::select_alpn, nullptr);
    }
}

#endif // KMX_AIO_FEATURE_QUIC
