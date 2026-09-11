/// @file src/kmx/aio/quic/primary_base_impl.cpp
/// @brief lsquic callbacks, engine and socket setup, and packet pump of the non-template QUIC engine core.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/quic/primary_base_impl.hpp>
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/buffer/handle.hpp>
    #include <kmx/aio/quic/base_engine.hpp>
    #include <kmx/aio/quic/engine.hpp>
    #include <kmx/aio/quic/settings.hpp>
    #include <kmx/aio/quic/stream_payload.hpp>
    #include <kmx/logger.hpp>

    #include <lsquic.h>

    #include <array>
    #include <atomic>
    #include <cerrno>
    #include <cstddef>
    #include <cstdint>
    #include <exception>
    #include <expected>
    #include <source_location>
    #include <string>
    #include <string_view>
    #include <system_error>
    #include <thread>
    #include <utility>
    #include <vector>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <sys/uio.h>
#endif

namespace kmx::aio::quic
{
    primary_base_impl::primary_base_impl(const spawn_stream_task_t spawn_stream_task) noexcept: spawn_stream_task_(spawn_stream_task)
    {
    }

    primary_base_impl::~primary_base_impl() noexcept
    {
        destroy_lsquic_engine();
        ::lsquic_global_cleanup();
    }

    void primary_base_impl::check_engine_thread(const std::string_view operation) noexcept
    {
        // Before init_lsquic() there is no engine and no owning thread yet, so nothing to violate.
        if ((engine_thread_ == std::thread::id {}) || (engine_thread_ == std::this_thread::get_id()))
            return;

        // Once. The pump calls through here per datagram, and a line per datagram would bury the first
        // one - which is the only one that says where the damage started.
        if (reported_foreign_thread_.exchange(true, std::memory_order_relaxed))
            return;

        logger::log(logger::level::error, std::source_location::current(),
                    "[QUIC] {} called off the engine thread; lsquic has no internal locking, so the engine state is "
                    "now unreliable",
                    operation);
    }

    void primary_base_impl::destroy_lsquic_engine() noexcept
    {
        if (lsquic_engine_)
        {
            check_engine_thread("engine teardown");

            ::lsquic_engine_destroy(lsquic_engine_);
            lsquic_engine_ = nullptr;
        }
    }

    int primary_base_impl::send_packets_out(void* ctx, const ::lsquic_out_spec* specs, const unsigned count)
    {
        auto* const self = static_cast<primary_base_impl*>(ctx);
        return detail::send_packets_out_fd(self->socket_fd_, specs, count);
    }

    ::lsquic_conn_ctx_t* primary_base_impl::on_new_conn(void* stream_if_ctx, ::lsquic_conn_t* conn)
    {
        static_cast<void>(conn);
        return reinterpret_cast<::lsquic_conn_ctx_t*>(stream_if_ctx);
    }

    void primary_base_impl::on_conn_closed(::lsquic_conn_t* conn)
    {
        auto* const self = reinterpret_cast<primary_base_impl*>(::lsquic_conn_get_ctx(conn));
        std::array<char, 512u> errbuf {};
        const auto status = ::lsquic_conn_status(conn, errbuf.data(), errbuf.size());
        logger::log(logger::level::info, std::source_location::current(), "[QUIC DEBUG] on_conn_closed called, status={} ({}), reason='{}'",
                    static_cast<int>(status), detail::conn_status_to_string(status), errbuf.data());

        if (self && self->is_client_)
            self->running_ = false;

        ::lsquic_conn_set_ctx(conn, nullptr);
    }

    void primary_base_impl::on_hsk_done(::lsquic_conn_t* conn, enum lsquic_hsk_status status)
    {
        auto* const self = reinterpret_cast<primary_base_impl*>(::lsquic_conn_get_ctx(conn));
        logger::log(logger::level::info, std::source_location::current(), "[QUIC DEBUG] on_hsk_done called, status={}, is_client_={}",
                    static_cast<int>(status), self ? self->is_client_ : false);

        if (self)
        {
            if (self->is_client_)
            {
                logger::log(logger::level::info, std::source_location::current(), "[QUIC DEBUG] on_hsk_done: client handshake completed");

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

    ::lsquic_stream_ctx_t* primary_base_impl::on_new_stream(void* stream_if_ctx, ::lsquic_stream_t* stream)
    {
        auto* const self = static_cast<primary_base_impl*>(stream_if_ctx);
        const bool is_local_stream = detail::is_local_initiated_stream(stream, self->is_client_);

        if (is_local_stream)
        {
            if (self->client_payload_streams_pending_ > 0u)
                --self->client_payload_streams_pending_;
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

    void primary_base_impl::on_read(::lsquic_stream_t* stream, ::lsquic_stream_ctx_t* /*ctx*/)
    {
        auto* const self = reinterpret_cast<primary_base_impl*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(stream)));

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

        // stream_payload_buffer is trivially default-constructible, so an empty optional here means one
        // thing only: every buffer in the pool is out on loan.
        auto leased = self->stream_payload_pool_.try_acquire();
        if (!leased)
        {
            // Park the stream rather than read it. Reading into a scratch buffer would move lsquic's read
            // offset and free the flow-control window, telling the peer the bytes arrived, and then throw
            // them away - a hole in a stream the peer was promised is reliable, and one it has no way to
            // detect. Leaving the bytes unread closes the window instead, which is how a QUIC receiver is
            // supposed to say "not now". resume_parked_reads() re-arms the stream once a buffer is back.
            ::lsquic_stream_wantread(stream, 0);
            if (self->read_parked_streams_.park(stream))
                logger::log(logger::level::warn, std::source_location::current(),
                            "QUIC payload pool exhausted; pausing stream reads until a buffer is returned");

            return;
        }

        buffer::handle<stream_payload_buffer> payload_storage = std::move(*leased);

        const ssize_t nr = ::lsquic_stream_read(stream, payload_storage->data(), payload_storage->size());
        if (nr > 0)
        {
            self->spawn_stream_task_(*self, stream, stream_payload {std::move(payload_storage), static_cast<std::size_t>(nr)});
            return;
        }

        handle_read_result(nr);
    }

    /// @brief Writes one payload to a stream, stopping at the first refusal.
    /// @param stream The stream to write to.
    /// @param payload The octets to write.
    /// @note A short write is logged and abandoned rather than retried: the stream is shut down straight
    ///       after, so there is nowhere for the remainder to go.
    static void write_payload(::lsquic_stream_t* const stream, const std::string& payload) noexcept
    {
        std::size_t written {};
        while (written < payload.size())
        {
            const ssize_t chunk = ::lsquic_stream_write(stream, payload.data() + written, payload.size() - written);
            if (chunk <= 0)
            {
                logger::log(logger::level::warn, std::source_location::current(), "QUIC client write failed on stream {}, written={}/{}",
                            static_cast<unsigned long long>(::lsquic_stream_id(stream)), written, payload.size());
                return;
            }

            written += static_cast<std::size_t>(chunk);
        }
    }

    bool primary_base_impl::write_post_handshake_stream(::lsquic_stream_t* const stream) noexcept
    {
        const auto bootstrap_it = post_handshake_streams_.find(stream);
        if (bootstrap_it == post_handshake_streams_.end())
            return false;

        if (post_handshake_stream_writer_)
            try
            {
                post_handshake_stream_writer_(stream);
            }
            catch (const std::exception& ex)
            {
                logger::log(logger::level::error, std::source_location::current(), "Post-handshake stream writer failed: {}", ex.what());
            }

        post_handshake_streams_.erase(bootstrap_it);
        ::lsquic_stream_wantwrite(stream, 0);
        ::lsquic_stream_wantread(stream, 1);
        return true;
    }

    void primary_base_impl::on_write(::lsquic_stream_t* stream, ::lsquic_stream_ctx_t* /*ctx*/)
    {
        auto* const self = reinterpret_cast<primary_base_impl*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(stream)));
        if (self->is_client_ && self->write_post_handshake_stream(stream))
            return;

        if (!self->is_client_ || self->client_payloads_.empty())
        {
            ::lsquic_stream_wantwrite(stream, 0);
            return;
        }

        std::string payload = std::move(self->client_payloads_.front());
        self->client_payloads_.pop();
        write_payload(stream, payload);

        ::lsquic_stream_flush(stream);
        ::lsquic_stream_shutdown(stream, 1);
        ::lsquic_stream_wantwrite(stream, 0);
        ::lsquic_stream_wantread(stream, 1);
    }

    struct ssl_ctx_st* primary_base_impl::get_ssl_ctx(void* peer_ctx, const struct sockaddr* /*local*/)
    {
        auto* const self = static_cast<primary_base_impl*>(peer_ctx);
        return reinterpret_cast<struct ssl_ctx_st*>(self->ssl_ctx_);
    }

    struct ssl_ctx_st* primary_base_impl::lookup_cert(void* cert_lu_ctx, const struct sockaddr* /*local*/, const char* /*sni*/)
    {
        auto* const self = static_cast<primary_base_impl*>(cert_lu_ctx);
        return reinterpret_cast<struct ssl_ctx_st*>(self->ssl_ctx_);
    }

    void primary_base_impl::on_close(::lsquic_stream_t* stream, ::lsquic_stream_ctx_t* /*ctx*/)
    {
        auto* const self = reinterpret_cast<primary_base_impl*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(stream)));
        if (!self)
            return;

        self->post_handshake_streams_.erase(stream);

        // A stream can be closed while parked, and resume_parked_reads() would then re-arm a pointer lsquic
        // has already destroyed.
        self->read_parked_streams_.forget(stream);
    }

    void primary_base_impl::configure_stream_if(::lsquic_stream_if& stream_if) noexcept
    {
        stream_if.on_new_conn = on_new_conn;
        stream_if.on_conn_closed = on_conn_closed;
        stream_if.on_new_stream = on_new_stream;
        stream_if.on_read = on_read;
        stream_if.on_write = on_write;
        stream_if.on_close = on_close;
        stream_if.on_hsk_done = on_hsk_done;
    }

    auto primary_base_impl::init_lsquic(const kmx::aio::quic::settings& config, unsigned lsquic_flags) -> expected_void_t
    {
        detail::maybe_enable_lsquic_debug_logging();

        if (::lsquic_global_init(lsquic_flags & LSENG_SERVER ? LSQUIC_GLOBAL_SERVER : LSQUIC_GLOBAL_CLIENT) != 0)
            return std::unexpected(error_from_errno(EINVAL));

        static ::lsquic_stream_if stream_if {};
        configure_stream_if(stream_if);

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

        // The engine belongs to whichever thread built it, which is the one running the executor that
        // called start() or connect(). Recorded here rather than in the constructor because the object
        // may well be built somewhere else; what matters is the thread that will drive it.
        engine_thread_ = std::this_thread::get_id();

        return {};
    }

    auto primary_base_impl::bind_socket(const ip_address_t ip, const port_t port) -> expected_void_t
    {
        auto sock_addr_result = make_socket_address(ip, port);
        if (!sock_addr_result)
            return std::unexpected(sock_addr_result.error());

        if (::bind(socket_fd_, reinterpret_cast<sockaddr*>(&sock_addr_result->storage), sock_addr_result->length) < 0)
            return std::unexpected(error_from_errno());

        // For ephemeral binds (port 0), propagate the kernel-assigned local address to lsquic.
        ::socklen_t local_len = sizeof(local_addr_);
        if (::getsockname(socket_fd_, reinterpret_cast<sockaddr*>(&local_addr_), &local_len) < 0)
            return std::unexpected(error_from_errno());

        return {};
    }

    auto primary_base_impl::setup_after_socket(const start_params& params) -> expected_void_t
    {
        ssl_ctx_ = params.ssl_ctx;

        if (auto bind_res = bind_socket(params.ip, params.port); !bind_res)
            return std::unexpected(bind_res.error());

        if (auto init_res = init_lsquic(params.config, LSENG_SERVER); !init_res)
            return std::unexpected(init_res.error());

        return {};
    }

    auto primary_base_impl::connect_socket_to(const socket_address& peer) -> expected_void_t
    {
        // Connected rather than left unbound: the kernel then fills in a source address, which the
        // engine needs, and refuses datagrams from anywhere but this peer.
        if (::connect(socket_fd_, reinterpret_cast<const sockaddr*>(&peer.storage), peer.length) < 0)
            return std::unexpected(error_from_errno());

        ::socklen_t local_len = sizeof(local_addr_);
        if (::getsockname(socket_fd_, reinterpret_cast<sockaddr*>(&local_addr_), &local_len) < 0)
            return std::unexpected(error_from_errno());
        return {};
    }

    auto primary_base_impl::connect_setup_after_socket(const connect_params& params) -> expected_void_t
    {
        ssl_ctx_ = params.ssl_ctx;
        is_client_ = true;
        client_payload_streams_pending_ = 0u;
        post_handshake_streams_pending_ = 0u;
        post_handshake_streams_.clear();

        // Bind to ephemeral port
        static constexpr std::array<std::uint8_t, 4u> any_ip {0, 0, 0, 0};
        if (auto bind_res = bind_socket(any_ip, 0); !bind_res)
            return std::unexpected(bind_res.error());

        if (auto init_res = init_lsquic(params.config, 0); !init_res)
            return std::unexpected(init_res.error());

        auto peer_addr_result = make_socket_address(params.peer_ip, params.peer_port);
        if (!peer_addr_result)
            return std::unexpected(peer_addr_result.error());
        if (auto connected = connect_socket_to(*peer_addr_result); !connected)
            return std::unexpected(connected.error());

        const char* host = params.hostname.empty() ? nullptr : params.hostname.c_str();

        ::lsquic_conn_t* const conn = ::lsquic_engine_connect(lsquic_engine_, N_LSQVER, reinterpret_cast<sockaddr*>(&local_addr_),
                                                              reinterpret_cast<sockaddr*>(&peer_addr_result->storage),
                                                              static_cast<void*>(this), nullptr, host, 0, nullptr, 0, nullptr, 0);
        if (!conn)
            return std::unexpected(error_from_errno());

        return {};
    }

    void primary_base_impl::set_client_payloads(const std::vector<std::string>& payloads)
    {
        clear_client_payload_queue();

        for (const auto& payload: payloads)
            if (!payload.empty())
                client_payloads_.push(payload);
    }

    void primary_base_impl::clear_client_payload_queue() noexcept
    {
        while (!client_payloads_.empty())
            client_payloads_.pop();
    }

    void primary_base_impl::prepare_recv_message(packet_buffer_t& packet_buf, ::sockaddr_storage& peer_addr, ::msghdr& msg,
                                                 ::iovec (&iov)[1u]) noexcept
    {
        iov[0].iov_base = packet_buf.data();
        iov[0].iov_len = packet_buf.size();
        msg.msg_name = &peer_addr;
        msg.msg_namelen = sizeof(peer_addr);
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
    }

    void primary_base_impl::resume_parked_reads() noexcept
    {
        if (stream_payload_pool_.available() == 0u)
            return;

        const auto resumed = read_parked_streams_.resume([](::lsquic_stream_t* const parked) { ::lsquic_stream_wantread(parked, 1); });
        if (resumed != 0u)
            logger::log(logger::level::debug, std::source_location::current(), "QUIC payload buffer returned; resuming {} stream read(s)",
                        resumed);
    }

    void primary_base_impl::drive_engine_once() noexcept
    {
        // The single funnel every connection tick passes through, on both the readiness and the completion
        // model, and so the one place worth checking: a packet fed in from a foreign thread reaches lsquic
        // through here, and so does every stream callback lsquic makes back into this object.
        check_engine_thread("engine processing");

        // Before the pass, so a stream re-armed here gets its on_read in the same one. Both models reach this
        // on every idle tick as well as on every datagram, so a buffer returned while the socket is quiet
        // still resumes within a tick.
        resume_parked_reads();

        ::lsquic_engine_process_conns(lsquic_engine_);
        ::lsquic_engine_send_unsent_packets(lsquic_engine_);
    }

    void primary_base_impl::bootstrap_initial_packets() noexcept
    {
        for (int i = 0; i < 10; ++i)
            drive_engine_once();
    }

    auto primary_base_impl::feed_packet_to_engine(const packet_buffer_t& packet_buf, const ssize_t recv_n,
                                                  const ::sockaddr_storage& peer_addr) -> expected_void_t
    {
        const int packet_in_res = ::lsquic_engine_packet_in(lsquic_engine_, reinterpret_cast<const unsigned char*>(packet_buf.data()),
                                                            static_cast<std::size_t>(recv_n), reinterpret_cast<::sockaddr*>(&local_addr_),
                                                            reinterpret_cast<::sockaddr*>(const_cast<::sockaddr_storage*>(&peer_addr)),
                                                            reinterpret_cast<void*>(this), 0);
        if (packet_in_res < 0)
        {
            logger::log(logger::level::error, std::source_location::current(), "lsquic_engine_packet_in failed: {}", packet_in_res);
            return std::unexpected(error_from_errno(EPROTO));
        }

        drive_engine_once();
        return {};
    }

    auto primary_base_impl::receive_once(packet_buffer_t& packet_buf, ::msghdr& msg, const ::sockaddr_storage& peer_addr)
        -> std::expected<bool, std::error_code>
    {
        const ssize_t recv_n = ::recvmsg(socket_fd_, &msg, MSG_DONTWAIT);
        if (recv_n < 0)
        {
            if (would_block(errno))
                return true;

            return std::unexpected(error_from_errno());
        }

        if (recv_n > 0)
            if (auto packet_res = feed_packet_to_engine(packet_buf, recv_n, peer_addr); !packet_res)
                return std::unexpected(packet_res.error());

        return false;
    }
}
