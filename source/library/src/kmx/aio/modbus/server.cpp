/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/modbus/server.hpp>
#if defined(KMX_AIO_FEATURE_MODBUS)
    #include <kmx/aio/modbus/detail/session.hpp>
    #include <kmx/aio/modbus/frame.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/tcp/listener.hpp>
    #include <kmx/aio/readiness/tcp/stream.hpp>

    #include <atomic>
    #include <cstdint>
    #include <stop_token>
    #include <unordered_map>
    #include <utility>

namespace kmx::aio::modbus
{
    // =========================================================================
    // Exception response builder
    // =========================================================================

    namespace
    {
        [[nodiscard]] std::vector<std::uint8_t>
        make_exception_response(const std::uint8_t request_fc, const exception_code ec) noexcept
        {
            return {static_cast<std::uint8_t>(request_fc | frame::exception_fc_flag),
                    static_cast<std::uint8_t>(ec)};
        }
    }

    // =========================================================================
    // pimpl
    // =========================================================================

    struct server::impl
    {
        std::unordered_map<std::uint8_t, request_handler> handlers_;
        std::stop_source stop_source_;

        // -----------------------------------------------------------------
        // Per-connection coroutine
        // -----------------------------------------------------------------

        task<void> handle_connection(readiness::executor& exec,
                                     file_descriptor fd,
                                     const server_config& config) noexcept(false)
        {
            readiness::tcp::stream stream {exec, std::move(fd)};
            const auto stop_token = stop_source_.get_token();

            while (!stop_token.stop_requested())
            {
                // Read 7-byte MBAP header
                std::array<std::uint8_t, frame::mbap_size> hdr_buf {};
                {
                    auto span = std::span<char>(
                        reinterpret_cast<char*>(hdr_buf.data()), hdr_buf.size()); // NOLINT
                    const auto r = co_await detail::read_exactly(stream, span);
                    if (!r)
                        co_return; // client disconnected or error
                }

                const auto hdr = frame::decode_mbap(hdr_buf);
                if (!hdr)
                    co_return;

                // unit_id check: accept if configured to accept all (0xFF) or matching
                if (config.unit_id != broadcast_unit_id && hdr->unit_id != config.unit_id)
                    co_return;

                // Read PDU: length field = unit_id(1) + PDU, so PDU = length - 1
                if (hdr->length < 1u)
                    co_return;

                const std::size_t pdu_len = static_cast<std::size_t>(hdr->length) - 1u;
                std::vector<std::uint8_t> pdu(pdu_len);

                if (pdu_len > 0u)
                {
                    auto span = std::span<char>(
                        reinterpret_cast<char*>(pdu.data()), pdu.size()); // NOLINT
                    if (const auto r = co_await detail::read_exactly(stream, span); !r)
                        co_return;
                }

                if (pdu.empty())
                    co_return;

                const std::uint8_t request_fc = pdu[0];

                // Dispatch to registered handler or return exception
                std::vector<std::uint8_t> response_pdu;
                if (const auto it = handlers_.find(request_fc); it != handlers_.end())
                {
                    server_request req {.unit_id = hdr->unit_id, .pdu = std::move(pdu)};
                    response_pdu = co_await it->second(std::move(req));
                }
                else
                {
                    response_pdu = make_exception_response(request_fc, exception_code::illegal_function);
                }

                // Build and send response ADU
                const auto resp_pdu_len = static_cast<std::uint16_t>(response_pdu.size());
                std::vector<std::uint8_t> response_adu(frame::mbap_size + resp_pdu_len);
                frame::encode_mbap(response_adu, hdr->transaction_id, resp_pdu_len, hdr->unit_id);
                std::ranges::copy(response_pdu,
                                  response_adu.begin() +
                                      static_cast<std::ptrdiff_t>(frame::mbap_size));

                const auto send_view = std::span<const char>(
                    reinterpret_cast<const char*>(response_adu.data()), // NOLINT
                    response_adu.size());
                if (const auto r = co_await stream.write_all(send_view); !r)
                    co_return;
            }
        }
    };

    // =========================================================================
    // server public API
    // =========================================================================

    server::server() noexcept: impl_(std::make_unique<impl>()) {}

    server::~server() noexcept = default;
    server::server(server&&) noexcept = default;
    server& server::operator=(server&&) noexcept = default;

    void server::set_handler(const function_code fc, request_handler handler)
    {
        impl_->handlers_[static_cast<std::uint8_t>(fc)] = std::move(handler);
    }

    task<std::expected<void, std::error_code>>
    server::serve(readiness::executor& exec, server_config config) noexcept(false)
    {
        const ipv4_storage_t bind_ip = config.bind_address.empty()
                                           ? any_ipv4
                                           : [&]() -> ipv4_storage_t {
            ipv4_storage_t ip {};
            parse_ipv4_address(config.bind_address, ip);
            return ip;
        }();

        readiness::tcp::listener listener {exec, make_ipv4_address(bind_ip), config.port};
        if (const auto r = listener.listen(); !r)
            co_return std::unexpected(r.error());

        const auto stop_token = impl_->stop_source_.get_token();

        while (!stop_token.stop_requested())
        {
            auto fd_result = co_await listener.accept();
            if (!fd_result)
            {
                if (stop_token.stop_requested())
                    break;
                co_return std::unexpected(fd_result.error());
            }

            exec.spawn(
                impl_->handle_connection(exec, std::move(*fd_result), config));
        }

        co_return {};
    }

    void server::stop() noexcept
    {
        impl_->stop_source_.request_stop();
    }

} // namespace kmx::aio::modbus
#endif // KMX_AIO_FEATURE_MODBUS
