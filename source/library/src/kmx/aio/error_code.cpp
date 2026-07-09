#include <kmx/aio/error_code.hpp>

#include <cerrno>

namespace kmx::aio
{
    std::string_view to_string(const error_code ec) noexcept
    {
        switch (ec)
        {
            case error_code::success:
                return "success";
            case error_code::connection_reset:
                return "connection reset";
            case error_code::connection_refused:
                return "connection refused";
            case error_code::connection_aborted:
                return "connection aborted";
            case error_code::connection_timeout:
                return "connection timeout";
            case error_code::broken_pipe:
                return "broken pipe";
            case error_code::end_of_stream:
                return "end of stream";
            case error_code::would_block:
                return "would block";
            case error_code::invalid_argument:
                return "invalid argument";
            case error_code::bad_descriptor:
                return "bad file descriptor";
            case error_code::address_in_use:
                return "address in use";
            case error_code::address_not_available:
                return "address not available";
            case error_code::not_connected:
                return "not connected";
            case error_code::operation_cancelled:
                return "operation cancelled";
            case error_code::buffer_overflow:
                return "buffer overflow";
            case error_code::tls_handshake_failed:
                return "TLS handshake failed";
            case error_code::tls_certificate_error:
                return "TLS certificate error";
            case error_code::quic_protocol_error:
                return "QUIC protocol error";
            case error_code::openonload_not_available:
                return "OpenOnload not available";
            case error_code::openonload_init_failed:
                return "OpenOnload init failed";
            case error_code::xdp_setup_failed:
                return "AF_XDP setup failed";
            case error_code::xdp_umem_registration_failed:
                return "AF_XDP UMEM registration failed";
            case error_code::xdp_ring_setup_failed:
                return "AF_XDP ring setup failed";
            case error_code::xdp_queue_bind_failed:
                return "AF_XDP queue bind failed";
            case error_code::spdk_env_init_failed:
                return "SPDK environment init failed";
            case error_code::spdk_probe_failed:
                return "SPDK probe failed";
            case error_code::spdk_queue_pair_failed:
                return "SPDK queue pair failed";
            case error_code::spdk_io_submit_failed:
                return "SPDK I/O submit failed";
            case error_code::spdk_io_completion_failed:
                return "SPDK I/O completion failed";
            case error_code::ring_full:
                return "ring full";
            case error_code::unsupported_operation:
                return "unsupported operation";
            case error_code::internal_error:
                return "internal error";
            case error_code::unknown:
                return "unknown error";
        }

        return "unknown error";
    }

    error_code from_errno(const int err) noexcept
    {
        switch (err)
        {
            case 0:
                return error_code::success;
            case ECONNRESET:
                return error_code::connection_reset;
            case ECONNREFUSED:
                return error_code::connection_refused;
            case ECONNABORTED:
                return error_code::connection_aborted;
            case ETIMEDOUT:
                return error_code::connection_timeout;
            case EPIPE:
                return error_code::broken_pipe;
            case EAGAIN:
                return error_code::would_block;
            case EINVAL:
                return error_code::invalid_argument;
            case EBADF:
                return error_code::bad_descriptor;
            case EADDRINUSE:
                return error_code::address_in_use;
            case EADDRNOTAVAIL:
                return error_code::address_not_available;
            case ENOTCONN:
                return error_code::not_connected;
            case ECANCELED:
                return error_code::operation_cancelled;
            default:
                return error_code::unknown;
        }
    }

    std::error_code to_std_error_code(const error_code ec) noexcept
    {
        switch (ec)
        {
            case error_code::success:
                return {};
            case error_code::connection_reset:
                return std::make_error_code(std::errc::connection_reset);
            case error_code::connection_refused:
                return std::make_error_code(std::errc::connection_refused);
            case error_code::connection_aborted:
                return std::make_error_code(std::errc::connection_aborted);
            case error_code::connection_timeout:
                return std::make_error_code(std::errc::timed_out);
            case error_code::broken_pipe:
                return std::make_error_code(std::errc::broken_pipe);
            case error_code::would_block:
                return std::make_error_code(std::errc::operation_would_block);
            case error_code::invalid_argument:
                return std::make_error_code(std::errc::invalid_argument);
            case error_code::bad_descriptor:
                return std::make_error_code(std::errc::bad_file_descriptor);
            case error_code::address_in_use:
                return std::make_error_code(std::errc::address_in_use);
            case error_code::address_not_available:
                return std::make_error_code(std::errc::address_not_available);
            case error_code::not_connected:
                return std::make_error_code(std::errc::not_connected);
            case error_code::operation_cancelled:
                return std::make_error_code(std::errc::operation_canceled);
            default:
                return std::make_error_code(std::errc::io_error);
        }
    }
} // namespace kmx::aio
