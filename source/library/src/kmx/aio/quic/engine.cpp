/// @file aio/quic/engine.cpp
/// @brief Consolidated QUIC engine implementation using explicit template instantiation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#if defined(KMX_AIO_FEATURE_QUIC)

    #ifndef PCH
        #include <charconv>
        #include <cstdlib>
        #include <kmx/aio/completion/executor.hpp>
        #include <kmx/aio/completion/udp/socket.hpp>
        #include <kmx/aio/quic/engine.hpp>
        #include <kmx/aio/readiness/executor.hpp>
        #include <kmx/aio/readiness/udp/socket.hpp>
    #endif

    #include "kmx/aio/quic/base_engine.hpp"

namespace kmx::aio::quic
{
    namespace detail
    {
        auto readiness_watchdog_tick_ns_from_env() noexcept -> long
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

        auto conn_status_to_string(const ::LSQUIC_CONN_STATUS status) noexcept -> std::string_view
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
                                 void (*on_conn_closed)(::lsquic_conn_t*),
                                 ::lsquic_stream_ctx_t* (*on_new_stream)(void*, ::lsquic_stream_t*),
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
    } // namespace detail

    // generic_engine implementation

    template <typename Executor, typename UdpSocket>
    struct generic_engine<Executor, UdpSocket>::impl: base_impl<Executor, UdpSocket>
    {
        using base_impl<Executor, UdpSocket>::base_impl;
    };

    template <typename Executor, typename UdpSocket>
    generic_engine<Executor, UdpSocket>::generic_engine(Executor& exec) noexcept: impl_(std::make_unique<impl>(exec))
    {
    }

    template <typename Executor, typename UdpSocket>
    generic_engine<Executor, UdpSocket>::~generic_engine() noexcept = default;

    template <typename Executor, typename UdpSocket>
    void generic_engine<Executor, UdpSocket>::set_stream_handler(stream_handler_t handler) noexcept
    {
        impl_->stream_handler_ = std::move(handler);
    }

    template <typename Executor, typename UdpSocket>
    task<std::expected<void, std::error_code>> generic_engine<Executor, UdpSocket>::start(const ip_address_t ip, const port_t port,
                                                                                          void* ssl_ctx,
                                                                                          const settings& config) noexcept(false)
    {
        if constexpr (requires { UdpSocket::create(impl_->exec_, ip_family(ip)); })
            co_return impl_->setup(UdpSocket::create(impl_->exec_, ip_family(ip)), ip, port, ssl_ctx, config);
        else
            co_return impl_->setup(UdpSocket::create(impl_->exec_.shared_from_this(), ip_family(ip)), ip, port, ssl_ctx, config);
    }

    template <typename Executor, typename UdpSocket>
    task<std::expected<void, std::error_code>> generic_engine<Executor, UdpSocket>::connect(const ip_address_t peer_ip,
                                                                                            const port_t peer_port,
                                                                                            const std::string& hostname,
                                                                                            const std::string& payload, void* ssl_ctx,
                                                                                            const settings& config) noexcept(false)
    {
        if constexpr (requires { UdpSocket::create(impl_->exec_, ip_family(peer_ip)); })
            co_return impl_->connect_setup(UdpSocket::create(impl_->exec_, ip_family(peer_ip)), peer_ip, peer_port, hostname, payload,
                                           ssl_ctx, config);
        else
            co_return impl_->connect_setup(UdpSocket::create(impl_->exec_.shared_from_this(), ip_family(peer_ip)), peer_ip, peer_port,
                                           hostname, payload, ssl_ctx, config);
    }

    template <typename Executor, typename UdpSocket>
    task<std::expected<void, std::error_code>> generic_engine<Executor, UdpSocket>::connect(
        const ip_address_t peer_ip, const port_t peer_port, const std::string& hostname, const std::vector<std::string>& payloads,
        void* ssl_ctx, const settings& config) noexcept(false)
    {
        if constexpr (requires { UdpSocket::create(impl_->exec_, ip_family(peer_ip)); })
            co_return impl_->connect_setup(UdpSocket::create(impl_->exec_, ip_family(peer_ip)), peer_ip, peer_port, hostname, payloads,
                                           ssl_ctx, config);
        else
            co_return impl_->connect_setup(UdpSocket::create(impl_->exec_.shared_from_this(), ip_family(peer_ip)), peer_ip, peer_port,
                                           hostname, payloads, ssl_ctx, config);
    }

    template <typename Executor, typename UdpSocket>
    task<std::expected<void, std::error_code>> generic_engine<Executor, UdpSocket>::process() noexcept(false)
    {
        co_return co_await impl_->process();
    }

    // Explicit instantiation
    template class generic_engine<kmx::aio::readiness::executor, kmx::aio::readiness::udp::socket>;
    template class generic_engine<kmx::aio::completion::executor, kmx::aio::completion::udp::socket>;

} // namespace kmx::aio::quic

#endif // KMX_AIO_FEATURE_QUIC
