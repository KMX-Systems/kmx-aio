/// @file aio/modbus/detail/session.hpp
/// @brief Transport-agnostic Modbus TCP request/response exchange helpers.
/// @details
/// Provides two free function templates that operate on any stream type
/// exposing `read(std::span<char>)` and `write_all(std::span<const char>)`
/// coroutine methods.  The session layer is intentionally stateless — the
/// transaction-ID counter and stream ownership live in the caller (client or
/// server pimpl).
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#if defined(KMX_AIO_FEATURE_MODBUS)
    #ifndef PCH
        #include <cstdint>
        #include <expected>
        #include <span>
        #include <system_error>
        #include <vector>
    #endif

    #include <kmx/aio/modbus/error.hpp>
    #include <kmx/aio/modbus/frame.hpp>
    #include <kmx/aio/task.hpp>

namespace kmx::aio::modbus::detail
{
    /// @brief Read exactly @p dest.size() bytes from @p stream.
    /// @details Loops on the stream's read() coroutine until all requested bytes
    ///          have been received, the stream reaches EOF, or an I/O error occurs.
    /// @tparam StreamT Any stream providing `result_task read(std::span<char>)`.
    /// @param stream The stream to read from.
    /// @param dest   Output span of the exact size to fill.
    /// @return Success, or an error on EOF / I/O failure.
    template <typename StreamT>
    [[nodiscard]] task<std::expected<void, std::error_code>>
    read_exactly(StreamT& stream, std::span<char> dest) noexcept(false)
    {
        std::size_t total = 0u;
        while (total < dest.size())
        {
            const auto result = co_await stream.read(dest.subspan(total));
            if (!result)
                co_return std::unexpected(result.error());
            if (*result == 0u)
                co_return std::unexpected(make_error_code(error::disconnected));
            total += *result;
        }
        co_return std::expected<void, std::error_code>();
    }

    /// @brief Send a Modbus ADU and receive the response PDU.
    /// @details Writes the full @p adu buffer, then reads the 7-byte MBAP
    ///          response header and the trailing PDU payload.  Validates the
    ///          protocol identifier, transaction identifier, and unit identifier.
    /// @tparam StreamT Any stream with `write_all` and `read` coroutines.
    /// @param stream       The connected stream.
    /// @param adu          Complete ADU bytes to send (MBAP header + PDU).
    /// @param expected_tid Transaction ID to validate in the response MBAP header.
    /// @param expected_unit Unit identifier to validate in the response MBAP header.
    /// @return Raw response PDU bytes (function code + data), or an error.
    template <typename StreamT>
    [[nodiscard]] task<std::expected<std::vector<std::uint8_t>, std::error_code>>
    exchange(StreamT& stream, std::span<const std::uint8_t> adu,
             const std::uint16_t expected_tid, const std::uint8_t expected_unit) noexcept(false)
    {
        // 1. Send request ADU
        const auto send_view = std::span<const char>(
            reinterpret_cast<const char*>(adu.data()), adu.size()); // NOLINT(*-reinterpret-cast)
        if (auto r = co_await stream.write_all(send_view); !r)
            co_return std::unexpected(r.error());

        // 2. Read 7-byte MBAP header (tid[2] + proto[2] + length[2] + unit_id[1])
        std::array<std::uint8_t, frame::mbap_size> hdr_buf {};
        {
            auto hdr_span = std::span<char>(
                reinterpret_cast<char*>(hdr_buf.data()), hdr_buf.size()); // NOLINT(*-reinterpret-cast)
            if (auto r = co_await read_exactly(stream, hdr_span); !r)
                co_return std::unexpected(r.error());
        }

        // 3. Parse and validate MBAP header
        const auto hdr = frame::decode_mbap(hdr_buf);
        if (!hdr)
            co_return std::unexpected(hdr.error());

        if (hdr->transaction_id != expected_tid)
            co_return std::unexpected(make_error_code(error::unexpected_transaction_id));

        if (hdr->unit_id != expected_unit)
            co_return std::unexpected(make_error_code(error::invalid_unit_id));

        // 4. The MBAP length field = unit_id(1) + PDU bytes; PDU length = length - 1
        if (hdr->length < 1u)
            co_return std::unexpected(make_error_code(error::malformed_frame));

        const std::size_t pdu_len = static_cast<std::size_t>(hdr->length) - 1u;
        std::vector<std::uint8_t> pdu(pdu_len);

        if (pdu_len > 0u)
        {
            auto pdu_span = std::span<char>(
                reinterpret_cast<char*>(pdu.data()), pdu.size()); // NOLINT(*-reinterpret-cast)
            if (auto r = co_await read_exactly(stream, pdu_span); !r)
                co_return std::unexpected(r.error());
        }

        co_return pdu;
    }

} // namespace kmx::aio::modbus::detail
#endif // KMX_AIO_FEATURE_MODBUS
