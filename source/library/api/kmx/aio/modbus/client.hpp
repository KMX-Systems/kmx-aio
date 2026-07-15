/// @file aio/modbus/client.hpp
/// @brief Asynchronous Modbus TCP client facade.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#if defined(KMX_AIO_FEATURE_MODBUS)
    #ifndef PCH
        #include <cstdint>
        #include <expected>
        #include <memory>
        #include <span>
        #include <system_error>
    #endif

    #include <kmx/aio/modbus/types.hpp>
    #include <kmx/aio/task.hpp>

namespace kmx::aio::readiness
{
    class executor;
}

namespace kmx::aio::modbus
{
    /// @brief Asynchronous Modbus TCP client.
    /// @details
    /// Presents coroutine-friendly operations over a single persistent TCP
    /// connection to a Modbus server.  All requests are lock-step
    /// (one in-flight at a time) and the transaction identifier is
    /// auto-incremented and validated on every exchange.
    ///
    /// @note @p host must be a dotted-decimal IPv4 address.
    class client
    {
    public:
        /// @brief Construct the client with the provided configuration.
        /// @param config Connection and protocol parameters.
        /// @param exec   Readiness executor owning the I/O event loop.
        explicit client(client_config config, readiness::executor& exec) noexcept;
        ~client() noexcept;

        client(const client&) = delete;
        client& operator=(const client&) = delete;
        client(client&&) noexcept;
        client& operator=(client&&) noexcept;

        /// @brief Establish the TCP connection to the configured host and port.
        /// @return Task resolving to success, or an error on connection failure.
        [[nodiscard]] task<std::expected<void, std::error_code>> connect() noexcept(false);

        /// @brief Close the TCP connection.
        /// @return Task resolving to success.
        [[nodiscard]] task<std::expected<void, std::error_code>> disconnect() noexcept(false);

        /// @brief Read contiguous holding registers (function code 0x03).
        /// @param address Starting register address.
        /// @param count   Number of registers to read (1–125).
        /// @return Decoded register values, or an error.
        [[nodiscard]] task<std::expected<register_values, std::error_code>>
        read_holding_registers(std::uint16_t address, std::uint16_t count) noexcept(false);

        /// @brief Read contiguous input registers (function code 0x04).
        /// @param address Starting register address.
        /// @param count   Number of registers to read (1–125).
        /// @return Decoded register values, or an error.
        [[nodiscard]] task<std::expected<register_values, std::error_code>>
        read_input_registers(std::uint16_t address, std::uint16_t count) noexcept(false);

        /// @brief Read contiguous coil statuses (function code 0x01).
        /// @param address Starting coil address.
        /// @param count   Number of coils to read (1–2000).
        /// @return Decoded coil values (0 or 1 per element), or an error.
        [[nodiscard]] task<std::expected<coil_values, std::error_code>>
        read_coils(std::uint16_t address, std::uint16_t count) noexcept(false);

        /// @brief Read contiguous discrete input statuses (function code 0x02).
        /// @param address Starting input address.
        /// @param count   Number of inputs to read (1–2000).
        /// @return Decoded coil values (0 or 1 per element), or an error.
        [[nodiscard]] task<std::expected<coil_values, std::error_code>>
        read_discrete_inputs(std::uint16_t address, std::uint16_t count) noexcept(false);

        /// @brief Write a single holding register (function code 0x06).
        /// @param address Target register address.
        /// @param value   16-bit value to write.
        /// @return Success or an error.
        [[nodiscard]] task<std::expected<void, std::error_code>>
        write_single_register(std::uint16_t address, std::uint16_t value) noexcept(false);

        /// @brief Write a single coil (function code 0x05).
        /// @param address Target coil address.
        /// @param on      @c true to set coil ON, @c false for OFF.
        /// @return Success or an error.
        [[nodiscard]] task<std::expected<void, std::error_code>>
        write_single_coil(std::uint16_t address, bool on) noexcept(false);

        /// @brief Write contiguous holding registers (function code 0x10).
        /// @param address Starting register address.
        /// @param values  Values to write (1–123 elements).
        /// @return Success or an error.
        [[nodiscard]] task<std::expected<void, std::error_code>>
        write_multiple_registers(std::uint16_t address,
                                 std::span<const std::uint16_t> values) noexcept(false);

        /// @brief Write contiguous coils (function code 0x0F).
        /// @param address Starting coil address.
        /// @param values  Coil values to write (0=OFF, non-zero=ON; 1–1968 elements).
        /// @return Success or an error.
        [[nodiscard]] task<std::expected<void, std::error_code>>
        write_multiple_coils(std::uint16_t address,
                             std::span<const std::uint8_t> values) noexcept(false);

        /// @brief Check whether the client currently has an open TCP connection.
        [[nodiscard]] bool is_connected() const noexcept;

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

} // namespace kmx::aio::modbus
#endif // KMX_AIO_FEATURE_MODBUS
