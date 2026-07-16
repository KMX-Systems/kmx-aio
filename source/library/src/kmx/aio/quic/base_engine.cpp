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
