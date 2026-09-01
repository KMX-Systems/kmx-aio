/// @file aio/quic/base_engine.cpp
/// @brief Non-template QUIC helper implementations shared by readiness and completion engines.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <kmx/aio/quic/base_engine.hpp>

#include <cstdio>

namespace kmx::aio::quic::detail
{
    namespace
    {
        int lsquic_log_to_stderr(void* /*logger_ctx*/, const char* buf, const std::size_t len) noexcept
        {
            return static_cast<int>(std::fwrite(buf, 1u, len, stderr));
        }
    }

    void maybe_enable_lsquic_debug_logging() noexcept
    {
        static bool initialized = false;
        if (initialized)
            return;
        initialized = true;

        const char* const level = std::getenv("KMX_AIO_QUIC_DEBUG_LOG");
        if (!level || level[0] == '\0')
            return;

        static const ::lsquic_logger_if logger_if {.log_buf = lsquic_log_to_stderr};
        ::lsquic_logger_init(&logger_if, nullptr, LLTS_HHMMSSUS);
        ::lsquic_set_log_level(level);
    }

    [[nodiscard]] auto readiness_watchdog_tick_ns_from_env() noexcept -> long
    {
        static constexpr long default_tick_ns = 10'000'000L; // 10 ms
        static constexpr long min_tick_ns = 1'000'000L;      // 1 ms
        static constexpr long max_tick_ns = 100'000'000L;    // 100 ms

        const char* const env = std::getenv("KMX_AIO_QUIC_READINESS_WATCHDOG_NS");
        if (!env || env[0] == '\0')
            return default_tick_ns;

        std::uint64_t parsed {};
        const char* const end = env + std::char_traits<char>::length(env);
        const auto [ptr, ec] = std::from_chars(env, end, parsed);
        if (ec != std::errc() || ptr != end)
            return default_tick_ns;

        if (parsed < static_cast<std::uint64_t>(min_tick_ns) || parsed > static_cast<std::uint64_t>(max_tick_ns))
            return default_tick_ns;

        return static_cast<long>(parsed);
    }

    [[nodiscard]] auto conn_status_to_string(const ::LSQUIC_CONN_STATUS status) noexcept -> std::string_view
    {
        switch (status)
        {
            case LSCONN_ST_HSK_IN_PROGRESS:
                return "LSCONN_ST_HSK_IN_PROGRESS";
            case LSCONN_ST_CONNECTED:
                return "LSCONN_ST_CONNECTED";
            case LSCONN_ST_HSK_FAILURE:
                return "LSCONN_ST_HSK_FAILURE";
            case LSCONN_ST_GOING_AWAY:
                return "LSCONN_ST_GOING_AWAY";
            case LSCONN_ST_TIMED_OUT:
                return "LSCONN_ST_TIMED_OUT";
            case LSCONN_ST_RESET:
                return "LSCONN_ST_RESET";
            case LSCONN_ST_USER_ABORTED:
                return "LSCONN_ST_USER_ABORTED";
            case LSCONN_ST_ERROR:
                return "LSCONN_ST_ERROR";
            case LSCONN_ST_CLOSED:
                return "LSCONN_ST_CLOSED";
            case LSCONN_ST_PEER_GOING_AWAY:
                return "LSCONN_ST_PEER_GOING_AWAY";
            case LSCONN_ST_VERNEG_FAILURE:
                return "LSCONN_ST_VERNEG_FAILURE";
            default:
                return "LSCONN_ST_UNKNOWN";
        }
    }

    void configure_stream_if(::lsquic_stream_if& stream_if, ::lsquic_conn_ctx_t* (*on_new_conn)(void*, ::lsquic_conn_t*),
                             void (*on_conn_closed)(::lsquic_conn_t*), ::lsquic_stream_ctx_t* (*on_new_stream)(void*, ::lsquic_stream_t*),
                             void (*on_read)(::lsquic_stream_t*, ::lsquic_stream_ctx_t*),
                             void (*on_write)(::lsquic_stream_t*, ::lsquic_stream_ctx_t*),
                             void (*on_close)(::lsquic_stream_t*, ::lsquic_stream_ctx_t*),
                             void (*on_hsk_done)(::lsquic_conn_t*, enum lsquic_hsk_status)) noexcept
    {
        stream_if.on_new_conn = on_new_conn;
        stream_if.on_conn_closed = on_conn_closed;
        stream_if.on_new_stream = on_new_stream;
        stream_if.on_read = on_read;
        stream_if.on_write = on_write;
        stream_if.on_close = on_close;
        stream_if.on_hsk_done = on_hsk_done;
    }

    void apply_lsquic_settings(::lsquic_engine_settings& lsquic_settings, const kmx::aio::quic::settings& config,
                               const unsigned lsquic_flags) noexcept
    {
        ::lsquic_engine_init_settings(&lsquic_settings, lsquic_flags);
        lsquic_settings.es_max_streams_in = config.max_streams_in;
        lsquic_settings.es_idle_timeout = config.idle_conn_timeout_sec;
        lsquic_settings.es_max_cfcw = config.max_cfcwnd;
    }

    [[nodiscard]] bool is_local_initiated_stream(const ::lsquic_stream_t* stream, const bool is_client) noexcept
    {
        const auto id = static_cast<std::uint64_t>(::lsquic_stream_id(stream));
        const std::uint64_t local_initiator_bit = is_client ? 0u : 1u;
        return (id & 0x1u) == local_initiator_bit;
    }

    int send_packets_out_fd(const int fd, const ::lsquic_out_spec* specs, const unsigned count) noexcept
    {
        unsigned sent {};
        ::msghdr msg {};

        for (; sent < count; ++sent)
        {
            msg = {};
            msg.msg_name = const_cast<void*>(reinterpret_cast<const void*>(specs[sent].dest_sa));
            msg.msg_namelen = (specs[sent].dest_sa->sa_family == AF_INET) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);

            std::vector<::iovec> iov;
            iov.reserve(specs[sent].iovlen);
            for (unsigned i = 0; i < specs[sent].iovlen; ++i)
                iov.push_back(::iovec {
                    .iov_base = const_cast<void*>(specs[sent].iov[i].iov_base),
                    .iov_len = specs[sent].iov[i].iov_len,
                });

            msg.msg_iov = iov.data();
            msg.msg_iovlen = iov.size();

            const ssize_t res = ::sendmsg(fd, &msg, 0);
            if (res < 0)
            {
                if (would_block(errno))
                    return sent > 0 ? static_cast<int>(sent) : -1;

                return sent > 0 ? static_cast<int>(sent) : -1;
            }
        }

        return static_cast<int>(sent);
    }
} // namespace kmx::aio::quic::detail

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

    void primary_base_impl::destroy_lsquic_engine() noexcept
    {
        if (lsquic_engine_)
        {
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
        (void) conn;
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
            self->spawn_stream_task_(*self, stream, stream_payload {std::move(payload_storage), static_cast<std::size_t>(nr)});
            return;
        }

        handle_read_result(nr);
    }

    void primary_base_impl::on_write(::lsquic_stream_t* stream, ::lsquic_stream_ctx_t* /*ctx*/)
    {
        auto* const self = reinterpret_cast<primary_base_impl*>(::lsquic_conn_get_ctx(::lsquic_stream_conn(stream)));
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
    }

    auto primary_base_impl::init_lsquic(const kmx::aio::quic::settings& config, unsigned lsquic_flags) -> expected_void_t
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

    auto primary_base_impl::setup_after_socket(const ip_address_t ip, const port_t port, void* ssl_ctx,
                                               const kmx::aio::quic::settings& config) -> expected_void_t
    {
        ssl_ctx_ = ssl_ctx;

        if (auto bind_res = bind_socket(ip, port); !bind_res)
            return std::unexpected(bind_res.error());

        if (auto init_res = init_lsquic(config, LSENG_SERVER); !init_res)
            return std::unexpected(init_res.error());

        return {};
    }

    auto primary_base_impl::connect_setup_after_socket(const ip_address_t peer_ip, const port_t peer_port, const std::string& hostname,
                                                       void* ssl_ctx, const kmx::aio::quic::settings& config) -> expected_void_t
    {
        ssl_ctx_ = ssl_ctx;
        is_client_ = true;
        client_payload_streams_pending_ = 0u;
        post_handshake_streams_pending_ = 0u;
        post_handshake_streams_.clear();

        // Bind to ephemeral port
        static constexpr std::array<std::uint8_t, 4u> any_ip {0, 0, 0, 0};
        if (auto bind_res = bind_socket(any_ip, 0); !bind_res)
            return std::unexpected(bind_res.error());

        if (auto init_res = init_lsquic(config, 0); !init_res)
            return std::unexpected(init_res.error());

        auto peer_addr_result = make_socket_address(peer_ip, peer_port);
        if (!peer_addr_result)
            return std::unexpected(peer_addr_result.error());

        if (::connect(socket_fd_, reinterpret_cast<sockaddr*>(&peer_addr_result->storage), peer_addr_result->length) < 0)
            return std::unexpected(error_from_errno());

        ::socklen_t local_len = sizeof(local_addr_);
        if (::getsockname(socket_fd_, reinterpret_cast<sockaddr*>(&local_addr_), &local_len) < 0)
            return std::unexpected(error_from_errno());

        const char* host = hostname.empty() ? nullptr : hostname.c_str();

        ::lsquic_conn_t* const conn = ::lsquic_engine_connect(lsquic_engine_, N_LSQVER, reinterpret_cast<sockaddr*>(&local_addr_),
                                                              reinterpret_cast<sockaddr*>(&peer_addr_result->storage),
                                                              static_cast<void*>(this), nullptr, host, 0, nullptr, 0, nullptr, 0);
        if (!conn)
            return std::unexpected(error_from_errno());

        return {};
    }

    void primary_base_impl::set_client_payload(const std::string& payload)
    {
        clear_client_payload_queue();

        if (!payload.empty())
            client_payloads_.push(payload);
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

    void primary_base_impl::drive_engine_once() noexcept
    {
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

    auto primary_base_impl::receive_once(packet_buffer_t& packet_buf, ::msghdr& msg,
                                         const ::sockaddr_storage& peer_addr) -> std::expected<bool, std::error_code>
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
} // namespace kmx::aio::quic
