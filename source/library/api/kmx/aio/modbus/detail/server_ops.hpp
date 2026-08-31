/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <array>
    #include <cstdint>
    #include <optional>
    #include <span>
    #include <unordered_map>
    #include <utility>
    #include <vector>
#endif

#include <kmx/aio/modbus/detail/session.hpp>
#include <kmx/aio/modbus/frame.hpp>
#include <kmx/aio/modbus/server.hpp>
#include <kmx/aio/modbus/types.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::modbus::detail
{
    /// @brief Builds the two-byte Modbus exception response for a failed request.
    /// @param request_fc The function code of the request being rejected.
    /// @param ec         The Modbus exception code to report.
    /// @return The PDU bytes: the function code with the exception flag set, then @p ec.
    [[nodiscard]] inline std::vector<std::uint8_t> make_exception_response(const std::uint8_t request_fc, const exception_code ec) noexcept
    {
        return {static_cast<std::uint8_t>(request_fc | frame::exception_fc_flag), static_cast<std::uint8_t>(ec)};
    }

    /// @brief Request-serving logic shared by the plain and TLS Modbus servers.
    /// @tparam ImplT The deriving server type, supplying the data-model callbacks (CRTP).
    template <typename ImplT>
    class server_ops
    {
    protected:
        /// @brief Serves one request from @p stream.
        /// @return True when the exchange completed and the connection can carry another request;
        ///         false when it cannot - the peer closed, an I/O error occurred, or the frame was
        ///         malformed, which leaves the stream positioned mid-frame and unusable. The caller
        ///         must stop reading from the connection when this returns false: looping on it
        ///         instead spins on a closed socket, or waits forever on one that will never speak
        ///         again, and either way the task never finishes.
        template <typename StreamT>
        [[nodiscard]] task<bool> process_request(StreamT& stream, const server_config& config) noexcept(false)
        {
            const auto hdr = co_await read_header(stream);
            if (!hdr || !addressed_to_us(*hdr, config))
                co_return false;

            auto pdu = co_await read_pdu(stream, hdr->length);
            if (!pdu)
                co_return false;

            const auto response_pdu = co_await dispatch(*hdr, std::move(*pdu));
            const auto response_adu = build_response_adu(*hdr, response_pdu);
            co_return co_await send_adu(stream, response_adu);
        }

    private:
        /// @brief Reads and decodes the MBAP header of the next request.
        /// @return The decoded header, or nothing when the peer closed, an I/O error occurred,
        ///         or the header is malformed.
        template <typename StreamT>
        [[nodiscard]] static task<std::optional<mbap_header>> read_header(StreamT& stream) noexcept(false)
        {
            std::array<std::uint8_t, frame::mbap_size> hdr_buf {};
            auto span = std::span<char>(reinterpret_cast<char*>(hdr_buf.data()), hdr_buf.size()); // NOLINT(*-reinterpret-cast)
            if (const auto r = co_await detail::read_exactly(stream, span); !r)
                co_return std::nullopt;

            const auto hdr = frame::decode_mbap(hdr_buf);
            if (!hdr)
                co_return std::nullopt;

            co_return *hdr;
        }

        /// @brief Tells whether a request carrying @p hdr is meant for this server.
        /// @details The PDU that follows the header has not been read, so a foreign unit
        ///          identifier leaves the stream mid-frame with no way to resynchronise: the
        ///          caller must end the connection rather than skip the request.
        [[nodiscard]] static bool addressed_to_us(const mbap_header& hdr, const server_config& config) noexcept
        {
            return (config.unit_id == broadcast_unit_id) || (hdr.unit_id == config.unit_id);
        }

        /// @brief Reads the PDU that follows a header whose length field is @p mbap_length.
        /// @param mbap_length MBAP length field: unit_id(1) + PDU bytes.
        /// @return The PDU bytes, or nothing when the length field is inconsistent, the PDU is
        ///         empty, or the read failed.
        template <typename StreamT>
        [[nodiscard]] static task<std::optional<std::vector<std::uint8_t>>> read_pdu(StreamT& stream,
                                                                                     const std::uint16_t mbap_length) noexcept(false)
        {
            if (mbap_length < 2u)
                co_return std::nullopt;

            std::vector<std::uint8_t> pdu(static_cast<std::size_t>(mbap_length) - 1u);
            auto span = std::span<char>(reinterpret_cast<char*>(pdu.data()), pdu.size()); // NOLINT(*-reinterpret-cast)
            if (const auto r = co_await detail::read_exactly(stream, span); !r)
                co_return std::nullopt;

            co_return pdu;
        }

        /// @brief Hands @p pdu to the handler registered for its function code.
        /// @return The handler's response PDU, or an @c illegal_function exception PDU when no
        ///         handler is registered for that function code.
        [[nodiscard]] task<std::vector<std::uint8_t>> dispatch(const mbap_header& hdr, std::vector<std::uint8_t> pdu) noexcept(false)
        {
            auto* self = static_cast<ImplT*>(this);
            const std::uint8_t request_fc = pdu[0];

            const auto it = self->handlers_.find(request_fc);
            if (it == self->handlers_.end())
                co_return make_exception_response(request_fc, exception_code::illegal_function);

            co_return co_await it->second(server_request {.unit_id = hdr.unit_id, .pdu = std::move(pdu)});
        }

        /// @brief Prefixes @p response_pdu with an MBAP header echoing @p hdr.
        [[nodiscard]] static std::vector<std::uint8_t> build_response_adu(const mbap_header& hdr,
                                                                          std::span<const std::uint8_t> response_pdu)
        {
            const auto pdu_len = static_cast<std::uint16_t>(response_pdu.size());
            std::vector<std::uint8_t> adu(frame::mbap_size + pdu_len);
            frame::encode_mbap(adu, hdr.transaction_id, pdu_len, hdr.unit_id);
            std::ranges::copy(response_pdu, adu.begin() + static_cast<std::ptrdiff_t>(frame::mbap_size));
            return adu;
        }

        /// @brief Writes @p adu in full.
        /// @return False when the write failed - the connection is no longer usable.
        template <typename StreamT>
        [[nodiscard]] static task<bool> send_adu(StreamT& stream, std::span<const std::uint8_t> adu) noexcept(false)
        {
            const auto view = std::span<const char>(reinterpret_cast<const char*>(adu.data()), adu.size()); // NOLINT(*-reinterpret-cast)
            const auto r = co_await stream.write_all(view);
            co_return r.has_value();
        }
    };

} // namespace kmx::aio::modbus::detail
