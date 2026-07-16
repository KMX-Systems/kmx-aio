/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <span>
    #include <unordered_map>
    #include <utility>
    #include <vector>
#endif

#include <kmx/aio/modbus/frame.hpp>
#include <kmx/aio/modbus/types.hpp>
#include <kmx/aio/modbus/server.hpp>
#include <kmx/aio/modbus/detail/session.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::modbus::detail
{
    [[nodiscard]] inline std::vector<std::uint8_t>
    make_exception_response(const std::uint8_t request_fc, const exception_code ec) noexcept
    {
        return {static_cast<std::uint8_t>(request_fc | frame::exception_fc_flag),
                static_cast<std::uint8_t>(ec)};
    }

    template <typename ImplT>
    class server_ops
    {
    protected:
        template <typename StreamT>
        [[nodiscard]] task<void>
        process_request(StreamT& stream, const server_config& config) noexcept(false)
        {
            auto* self = static_cast<ImplT*>(this);

            // Phase 1: Read and validate MBAP header
            std::array<std::uint8_t, frame::mbap_size> hdr_buf {};
            {
                auto span = std::span<char>(reinterpret_cast<char*>(hdr_buf.data()), hdr_buf.size());
                const auto r = co_await detail::read_exactly(stream, span);
                if (!r)
                    co_return;
            }

            const auto hdr = frame::decode_mbap(hdr_buf);
            if (!hdr)
                co_return;

            if ((config.unit_id != broadcast_unit_id) && (hdr->unit_id != config.unit_id))
                co_return;

            // Phase 2: Read PDU
            if (hdr->length < 1u)
                co_return;

            const std::size_t pdu_len = static_cast<std::size_t>(hdr->length) - 1u;
            std::vector<std::uint8_t> pdu(pdu_len);

            if (pdu_len > 0u)
            {
                auto span = std::span<char>(reinterpret_cast<char*>(pdu.data()), pdu.size());
                if (const auto r = co_await detail::read_exactly(stream, span); !r)
                    co_return;
            }

            if (pdu.empty())
                co_return;

            // Phase 3: Dispatch to handler
            const std::uint8_t request_fc = pdu[0];
            std::vector<std::uint8_t> response_pdu;

            if (const auto it = self->handlers_.find(request_fc); it != self->handlers_.end())
            {
                server_request req {.unit_id = hdr->unit_id, .pdu = std::move(pdu)};
                response_pdu = co_await it->second(std::move(req));
            }
            else
                response_pdu = make_exception_response(request_fc, exception_code::illegal_function);

            // Phase 4: Build response ADU
            const auto resp_pdu_len = static_cast<std::uint16_t>(response_pdu.size());
            std::vector<std::uint8_t> response_adu(frame::mbap_size + resp_pdu_len);
            frame::encode_mbap(response_adu, hdr->transaction_id, resp_pdu_len, hdr->unit_id);
            std::ranges::copy(response_pdu,
                              response_adu.begin() + static_cast<std::ptrdiff_t>(frame::mbap_size));

            // Phase 5: Send response
            const auto send_view = std::span<const char>(
                reinterpret_cast<const char*>(response_adu.data()), response_adu.size());
            if (const auto r = co_await stream.write_all(send_view); !r)
                co_return;
        }
    };

} // namespace kmx::aio::modbus::detail
