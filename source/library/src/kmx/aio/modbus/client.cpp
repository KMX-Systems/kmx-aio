/// @file src/kmx/aio/modbus/client.cpp
/// @brief The compiled body of the Modbus TCP client, running over a readiness TCP stream.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/modbus/client.hpp>
#if defined(KMX_AIO_FEATURE_MODBUS)
    #ifndef PCH
        #include <kmx/aio/error_code.hpp>
        #include <kmx/aio/modbus/detail/client_ops.hpp>
        #include <kmx/aio/modbus/frame.hpp>
        #include <kmx/aio/readiness/basic_types.hpp>
        #include <kmx/aio/readiness/executor.hpp>
        #include <kmx/aio/readiness/tcp/connect.hpp>
        #include <kmx/aio/readiness/tcp/stream.hpp>

        #include <cstdint>
        #include <cstring>
        #include <optional>
        #include <utility>
        #include <netinet/in.h>
        #include <sys/socket.h>
    #endif

namespace kmx::aio::modbus
{
    // Type aliases for common async result types
    using async_result = task_returning_expected_void_t;
    using async_register_result = task<std::expected<register_values, std::error_code>>;
    using async_coil_result = task<std::expected<coil_values, std::error_code>>;
    using async_fd_result = task<file_descriptor::expected_t>;

    struct client::impl: detail::client_ops<client::impl, readiness::tcp::stream>
    {
        readiness::executor& exec_;
        client_config config_;
        std::optional<readiness::tcp::stream> stream_;
        std::uint16_t next_tid_ {};

        explicit impl(client_config config, readiness::executor& exec) noexcept: exec_(exec), config_(std::move(config)) {}

        [[nodiscard]] task_returning_expected_void_t connect() noexcept(false)
        {
            if (stream_.has_value())
                co_return expected_void_t();

            // Parse IPv4 host
            ipv4::storage_t ip_storage {};
            if (!ipv4::parse_address(config_.host, ip_storage))
                co_return std::unexpected(make_error_code(error::invalid_configuration));

            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_port = htons(config_.port);
            std::memcpy(&address.sin_addr.s_addr, ip_storage.data(), ip_storage.size());

            // Create, register and connect the socket. A cancelled wait keeps its own error; every other failure is
            // reported as it always was, as a failed connection.
            auto connected = co_await readiness::tcp::connect(exec_, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
            if (!connected)
            {
                const auto cancelled = connected.error() == to_std_error_code(error_code::operation_cancelled);
                co_return std::unexpected(cancelled ? connected.error() : make_error_code(error::connection_failed));
            }

            stream_.emplace(exec_, std::move(*connected));
            co_return expected_void_t();
        }

        [[nodiscard]] async_result disconnect() noexcept(false)
        {
            stream_.reset();
            co_return expected_void_t();
        }
    };

    client::client(client_config config, readiness::executor& exec) noexcept: impl_(std::make_unique<impl>(std::move(config), exec))
    {
    }

    client::~client() noexcept = default;
    client::client(client&&) noexcept = default;
    client& client::operator=(client&&) noexcept = default;

    async_result client::connect() noexcept(false)
    {
        return impl_->connect();
    }

    async_result client::disconnect() noexcept(false)
    {
        return impl_->disconnect();
    }

    async_register_result client::read_holding_registers(const std::uint16_t address, const std::uint16_t count) noexcept(false)
    {
        return impl_->read_registers(function_code::read_holding_registers, address, count);
    }

    async_register_result client::read_input_registers(const std::uint16_t address, const std::uint16_t count) noexcept(false)
    {
        return impl_->read_registers(function_code::read_input_registers, address, count);
    }

    async_coil_result client::read_coils(const std::uint16_t address, const std::uint16_t count) noexcept(false)
    {
        return impl_->read_coils_impl(function_code::read_coils, address, count);
    }

    async_coil_result client::read_discrete_inputs(const std::uint16_t address, const std::uint16_t count) noexcept(false)
    {
        return impl_->read_coils_impl(function_code::read_discrete_inputs, address, count);
    }

    async_result client::write_single_register(const std::uint16_t address, const std::uint16_t value) noexcept(false)
    {
        const auto pdu = frame::encode_write_single_register(address, value);
        auto self = impl_.get();
        auto response = co_await self->exchange_pdu(pdu);
        if (!response)
            co_return std::unexpected(response.error());
        co_return frame::decode_write_single_response(*response, function_code::write_single_register);
    }

    async_result client::write_single_coil(const std::uint16_t address, const bool on) noexcept(false)
    {
        const auto pdu = frame::encode_write_single_coil(address, on);
        auto self = impl_.get();
        auto response = co_await self->exchange_pdu(pdu);
        if (!response)
            co_return std::unexpected(response.error());
        co_return frame::decode_write_single_response(*response, function_code::write_single_coil);
    }

    async_result client::write_multiple_registers(const std::uint16_t address, const std::span<const std::uint16_t> values) noexcept(false)
    {
        auto self = impl_.get();
        const auto pdu_result = frame::encode_write_multiple_registers(address, values);
        if (!pdu_result)
            co_return std::unexpected(pdu_result.error());

        auto response = co_await self->exchange_pdu(*pdu_result);
        if (!response)
            co_return std::unexpected(response.error());
        co_return frame::decode_write_multiple_response(*response, function_code::write_multiple_registers);
    }

    async_result client::write_multiple_coils(const std::uint16_t address, const cspan_uint8_t values) noexcept(false)
    {
        auto self = impl_.get();
        const auto pdu_result = frame::encode_write_multiple_coils(address, values);
        if (!pdu_result)
            co_return std::unexpected(pdu_result.error());

        auto response = co_await self->exchange_pdu(*pdu_result);
        if (!response)
            co_return std::unexpected(response.error());
        co_return frame::decode_write_multiple_response(*response, function_code::write_multiple_coils);
    }

    bool client::is_connected() const noexcept
    {
        return impl_->stream_.has_value();
    }

}
#endif // KMX_AIO_FEATURE_MODBUS
