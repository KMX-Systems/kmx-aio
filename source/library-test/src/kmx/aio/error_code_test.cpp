/// @file aio/error_code_test.cpp
/// @brief Unit tests for the error_code vocabulary and its errno / std::error_code mappings.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/error_code.hpp>

#include <cerrno>
#include <set>
#include <string_view>
#include <vector>

namespace kmx::aio::error_code_test_detail
{
    // Every enumerator, in declaration order. Kept explicit rather than derived from a sentinel: a new
    // enumerator should fail this list loudly, which is the point of the exhaustiveness test below.
    inline const std::vector<error_code> all_codes {
        error_code::success,
        error_code::connection_reset,
        error_code::connection_refused,
        error_code::connection_aborted,
        error_code::connection_timeout,
        error_code::broken_pipe,
        error_code::end_of_stream,
        error_code::would_block,
        error_code::invalid_argument,
        error_code::bad_descriptor,
        error_code::address_in_use,
        error_code::address_not_available,
        error_code::not_connected,
        error_code::operation_cancelled,
        error_code::buffer_overflow,
        error_code::tls_handshake_failed,
        error_code::tls_certificate_error,
        error_code::quic_protocol_error,
        error_code::openonload_not_available,
        error_code::openonload_init_failed,
        error_code::xdp_setup_failed,
        error_code::xdp_umem_registration_failed,
        error_code::xdp_ring_setup_failed,
        error_code::xdp_queue_bind_failed,
        error_code::spdk_env_init_failed,
        error_code::spdk_probe_failed,
        error_code::spdk_queue_pair_failed,
        error_code::spdk_io_submit_failed,
        error_code::spdk_io_completion_failed,
        error_code::ring_full,
        error_code::unsupported_operation,
        error_code::internal_error,
        error_code::unknown,
    };
} // namespace kmx::aio::error_code_test_detail

namespace kmx::aio
{
    using error_code_test_detail::all_codes;

    TEST_CASE("to_string describes every error code", "[core][error_code][to_string]")
    {
        // The switch has one arm per enumerator; walking the whole list is what drives them all.
        for (const auto ec: all_codes)
        {
            const auto text = to_string(ec);
            CAPTURE(static_cast<std::uint32_t>(ec));
            CHECK_FALSE(text.empty());
        }
    }

    TEST_CASE("to_string returns the documented text", "[core][error_code][to_string]")
    {
        CHECK(to_string(error_code::success) == "success");
        CHECK(to_string(error_code::connection_reset) == "connection reset");
        CHECK(to_string(error_code::connection_refused) == "connection refused");
        CHECK(to_string(error_code::connection_aborted) == "connection aborted");
        CHECK(to_string(error_code::connection_timeout) == "connection timeout");
        CHECK(to_string(error_code::broken_pipe) == "broken pipe");
        CHECK(to_string(error_code::end_of_stream) == "end of stream");
        CHECK(to_string(error_code::would_block) == "would block");
        CHECK(to_string(error_code::invalid_argument) == "invalid argument");
        CHECK(to_string(error_code::bad_descriptor) == "bad file descriptor");
        CHECK(to_string(error_code::address_in_use) == "address in use");
        CHECK(to_string(error_code::address_not_available) == "address not available");
        CHECK(to_string(error_code::not_connected) == "not connected");
        CHECK(to_string(error_code::operation_cancelled) == "operation cancelled");
        CHECK(to_string(error_code::buffer_overflow) == "buffer overflow");
        CHECK(to_string(error_code::tls_handshake_failed) == "TLS handshake failed");
        CHECK(to_string(error_code::tls_certificate_error) == "TLS certificate error");
        CHECK(to_string(error_code::quic_protocol_error) == "QUIC protocol error");
        CHECK(to_string(error_code::openonload_not_available) == "OpenOnload not available");
        CHECK(to_string(error_code::openonload_init_failed) == "OpenOnload init failed");
        CHECK(to_string(error_code::xdp_setup_failed) == "AF_XDP setup failed");
        CHECK(to_string(error_code::xdp_umem_registration_failed) == "AF_XDP UMEM registration failed");
        CHECK(to_string(error_code::xdp_ring_setup_failed) == "AF_XDP ring setup failed");
        CHECK(to_string(error_code::xdp_queue_bind_failed) == "AF_XDP queue bind failed");
        CHECK(to_string(error_code::spdk_env_init_failed) == "SPDK environment init failed");
        CHECK(to_string(error_code::spdk_probe_failed) == "SPDK probe failed");
        CHECK(to_string(error_code::spdk_queue_pair_failed) == "SPDK queue pair failed");
        CHECK(to_string(error_code::spdk_io_submit_failed) == "SPDK I/O submit failed");
        CHECK(to_string(error_code::spdk_io_completion_failed) == "SPDK I/O completion failed");
        CHECK(to_string(error_code::ring_full) == "ring full");
        CHECK(to_string(error_code::unsupported_operation) == "unsupported operation");
        CHECK(to_string(error_code::internal_error) == "internal error");
        CHECK(to_string(error_code::unknown) == "unknown error");
    }

    TEST_CASE("to_string descriptions are distinct", "[core][error_code][to_string]")
    {
        // Two codes sharing a description makes a log line ambiguous about which condition occurred.
        std::set<std::string_view> seen;
        for (const auto ec: all_codes)
        {
            CAPTURE(to_string(ec));
            CHECK(seen.insert(to_string(ec)).second);
        }
    }

    TEST_CASE("to_string falls back for an out-of-range value", "[core][error_code][to_string]")
    {
        // The trailing return after the switch: reachable only through a value no enumerator names.
        const auto bogus = static_cast<error_code>(0xffffu);
        CHECK(to_string(bogus) == "unknown error");
    }

    TEST_CASE("from_errno maps the POSIX errors the library reacts to", "[core][error_code][from_errno]")
    {
        CHECK(from_errno(0) == error_code::success);
        CHECK(from_errno(ECONNRESET) == error_code::connection_reset);
        CHECK(from_errno(ECONNREFUSED) == error_code::connection_refused);
        CHECK(from_errno(ECONNABORTED) == error_code::connection_aborted);
        CHECK(from_errno(ETIMEDOUT) == error_code::connection_timeout);
        CHECK(from_errno(EPIPE) == error_code::broken_pipe);
        CHECK(from_errno(EAGAIN) == error_code::would_block);
        CHECK(from_errno(EINVAL) == error_code::invalid_argument);
        CHECK(from_errno(EBADF) == error_code::bad_descriptor);
        CHECK(from_errno(EADDRINUSE) == error_code::address_in_use);
        CHECK(from_errno(EADDRNOTAVAIL) == error_code::address_not_available);
        CHECK(from_errno(ENOTCONN) == error_code::not_connected);
        CHECK(from_errno(ECANCELED) == error_code::operation_cancelled);
    }

    TEST_CASE("from_errno reports anything unmapped as unknown", "[core][error_code][from_errno]")
    {
        CHECK(from_errno(ENOSYS) == error_code::unknown);
        CHECK(from_errno(ENOMEM) == error_code::unknown);
        CHECK(from_errno(-1) == error_code::unknown);
        CHECK(from_errno(0x7fffffff) == error_code::unknown);
    }

    TEST_CASE("to_std_error_code maps onto std::errc", "[core][error_code][std_interop]")
    {
        CHECK(to_std_error_code(error_code::connection_reset) == std::errc::connection_reset);
        CHECK(to_std_error_code(error_code::connection_refused) == std::errc::connection_refused);
        CHECK(to_std_error_code(error_code::connection_aborted) == std::errc::connection_aborted);
        CHECK(to_std_error_code(error_code::connection_timeout) == std::errc::timed_out);
        CHECK(to_std_error_code(error_code::broken_pipe) == std::errc::broken_pipe);
        CHECK(to_std_error_code(error_code::would_block) == std::errc::operation_would_block);
        CHECK(to_std_error_code(error_code::invalid_argument) == std::errc::invalid_argument);
        CHECK(to_std_error_code(error_code::bad_descriptor) == std::errc::bad_file_descriptor);
        CHECK(to_std_error_code(error_code::address_in_use) == std::errc::address_in_use);
        CHECK(to_std_error_code(error_code::address_not_available) == std::errc::address_not_available);
        CHECK(to_std_error_code(error_code::not_connected) == std::errc::not_connected);
        CHECK(to_std_error_code(error_code::operation_cancelled) == std::errc::operation_canceled);
    }

    TEST_CASE("to_std_error_code maps success to a falsy error_code", "[core][error_code][std_interop]")
    {
        const auto ec = to_std_error_code(error_code::success);
        CHECK_FALSE(static_cast<bool>(ec));
        CHECK(ec.value() == 0);
    }

    TEST_CASE("to_std_error_code funnels unmapped codes to io_error", "[core][error_code][std_interop]")
    {
        // The default arm: every code with no std::errc counterpart still has to surface as a failure.
        for (const auto ec: {error_code::end_of_stream, error_code::buffer_overflow, error_code::tls_handshake_failed,
                             error_code::tls_certificate_error, error_code::quic_protocol_error, error_code::openonload_not_available,
                             error_code::openonload_init_failed, error_code::xdp_setup_failed, error_code::xdp_umem_registration_failed,
                             error_code::xdp_ring_setup_failed, error_code::xdp_queue_bind_failed, error_code::spdk_env_init_failed,
                             error_code::spdk_probe_failed, error_code::spdk_queue_pair_failed, error_code::spdk_io_submit_failed,
                             error_code::spdk_io_completion_failed, error_code::ring_full, error_code::unsupported_operation,
                             error_code::internal_error, error_code::unknown})
        {
            CAPTURE(to_string(ec));
            CHECK(to_std_error_code(ec) == std::errc::io_error);
        }
    }

    TEST_CASE("every error code converts without throwing", "[core][error_code]")
    {
        // to_string / to_std_error_code are noexcept; this pins the whole enum against that contract and
        // drives the remaining switch arms in one sweep.
        for (const auto ec: all_codes)
        {
            static_assert(noexcept(to_string(error_code::success)));
            static_assert(noexcept(to_std_error_code(error_code::success)));
            const auto std_ec = to_std_error_code(ec);
            const bool is_success = (ec == error_code::success);
            CAPTURE(to_string(ec));
            CHECK(static_cast<bool>(std_ec) != is_success);
        }
    }

    TEST_CASE("from_errno round-trips through to_std_error_code", "[core][error_code][round_trip]")
    {
        // A failing syscall sets errno; the library maps it to error_code and hands std::error_code to
        // callers. Both hops have to agree for the pair to be usable together.
        struct expectation
        {
            int err;
            std::errc expected;
        };

        for (const auto& [err, expected]: {expectation {ECONNRESET, std::errc::connection_reset},
                                           expectation {ECONNREFUSED, std::errc::connection_refused},
                                           expectation {EPIPE, std::errc::broken_pipe},
                                           expectation {EBADF, std::errc::bad_file_descriptor},
                                           expectation {EINVAL, std::errc::invalid_argument},
                                           expectation {ECANCELED, std::errc::operation_canceled}})
        {
            CAPTURE(err);
            CHECK(to_std_error_code(from_errno(err)) == expected);
        }
    }
}
