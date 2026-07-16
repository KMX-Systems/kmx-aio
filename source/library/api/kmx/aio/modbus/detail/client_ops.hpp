/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/modbus/frame.hpp>
    #include <kmx/aio/modbus/types.hpp>
    #include <kmx/aio/modbus/detail/session.hpp>
    #include <kmx/aio/task.hpp>

    #include <cstdint>
    #include <expected>
    #include <span>
    #include <utility>
    #include <vector>
#endif

namespace kmx::aio::modbus::detail
{
    /// @brief Template mixin providing shared client operation implementations.
    /// @tparam ImplT The concrete impl type (must have: exec_, config_, stream_, next_tid_)
    /// @tparam StreamT The stream type (readiness::tcp::stream or readiness::tls::stream)
    template <typename ImplT, typename StreamT>
    class client_ops
    {
    public:
        /// @brief Encode a complete ADU, send it, and return the raw response PDU.
        [[nodiscard]] task<std::expected<std::vector<std::uint8_t>, std::error_code>>
        exchange_pdu(const std::span<const std::uint8_t> pdu_bytes) noexcept(false)
        {
            auto* self = static_cast<ImplT*>(this);
            if (!self->stream_.has_value())
                co_return std::unexpected(make_error_code(error::disconnected));

            const auto tid = self->next_tid_++;
            const auto pdu_len = static_cast<std::uint16_t>(pdu_bytes.size());

            // Build ADU: 7-byte MBAP header + PDU
            std::vector<std::uint8_t> adu(frame::mbap_size + pdu_len);
            frame::encode_mbap(adu, tid, pdu_len, self->config_.unit_id);
            std::ranges::copy(pdu_bytes,
                              adu.begin() + static_cast<std::ptrdiff_t>(frame::mbap_size));

            co_return co_await exchange(*self->stream_, adu, tid, self->config_.unit_id);
        }

        /// @brief Read holding or input registers.
        [[nodiscard]] task<std::expected<register_values, std::error_code>>
        read_registers(const function_code fc, const std::uint16_t address,
                       const std::uint16_t count) noexcept(false)
        {
            const auto pdu_result = frame::encode_read_request(fc, address, count);
            if (!pdu_result)
                co_return std::unexpected(pdu_result.error());

            auto response = co_await exchange_pdu(*pdu_result);
            if (!response)
                co_return std::unexpected(response.error());

            co_return frame::decode_read_registers_response(*response, fc, count);
        }

        /// @brief Read coils or discrete inputs.
        [[nodiscard]] task<std::expected<coil_values, std::error_code>>
        read_coils_impl(const function_code fc, const std::uint16_t address,
                        const std::uint16_t count) noexcept(false)
        {
            const auto pdu_result = frame::encode_read_request(fc, address, count);
            if (!pdu_result)
                co_return std::unexpected(pdu_result.error());

            auto response = co_await exchange_pdu(*pdu_result);
            if (!response)
                co_return std::unexpected(response.error());

            co_return frame::decode_read_coils_response(*response, fc, count);
        }
    };

} // namespace kmx::aio::modbus::detail
