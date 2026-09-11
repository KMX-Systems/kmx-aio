/// @file api/kmx/aio/knx/telegram.hpp
/// @brief A received cEMI message together with the octets it was decoded from.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/cemi_frame.hpp>
        #include <kmx/aio/knx/dpt/traits.hpp>
        #include <kmx/aio/knx/dpt/value_view.hpp>
        #include <kmx/aio/task.hpp>

        #include <cstdint>
        #include <expected>
        #include <system_error>
    #endif

namespace kmx::aio::knx
{
    /// @brief A received cEMI message together with the octets it was decoded from.
    /// @details The decoded frame names its payload by offset, so the octets have to travel with it. Owning
    ///          them here is what lets a telegram outlive the receive buffer it arrived in.
    struct telegram
    {
        /// @brief The decoded message.
        cemi_frame frame {};
        /// @brief The cEMI octets the message was decoded from.
        byte_buffer_t bytes {};

        /// @brief Returns the application payload.
        [[nodiscard]] cspan_uint8_t payload() const noexcept { return frame.payload(bytes); }
        /// @brief Returns the value view a datapoint decoder accepts.
        [[nodiscard]] dpt::value_view value() const noexcept { return dpt::make_value_view(frame, bytes); }

        /// @brief Decodes the application value as the named datapoint main type.
        /// @tparam Main The datapoint main type.
        /// @return The decoded value, or the reason it could not be decoded.
        template <std::uint16_t Main>
        [[nodiscard]] dpt::decode_result_t<Main> value_as() const noexcept
        {
            return dpt::traits<Main>::decode(value());
        }
    };

    /// @brief A received telegram, or the error explaining why none was obtained.
    using telegram_result_t = std::expected<telegram, std::error_code>;
    /// @brief Task yielding a received telegram or the error that stopped the receive.
    using telegram_task_t = task<telegram_result_t>;
}
#endif // KMX_AIO_FEATURE_KNX
