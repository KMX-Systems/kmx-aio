/// @file inc/kmx/aio/knx/secure/detail/wrapper_crypto.hpp
/// @brief The SECURE_WRAPPER and TIMER_NOTIFY cryptography, over an explicit backend.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details The public functions in `wrapper.hpp` and `timer_notify.hpp` call these with the EVP backend. Tests
///          call them with a failing backend, which is how the crypto failure paths are reached without any
///          global switch (§5.2).
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/frame.hpp>
        #include <kmx/aio/knx/secure/detail/ccm.hpp>
        #include <kmx/aio/knx/secure/timer_notify.hpp>
        #include <kmx/aio/knx/secure/wrapper.hpp>

        #include <array>
        #include <cstddef>
        #include <cstdint>
    #endif

namespace kmx::aio::knx::secure::detail
{
    /// @brief The KNXnet/IP header of every TIMER_NOTIFY, which is the associated data of its MAC.
    using timer_notify_header_t = std::array<std::uint8_t, frame::communication_header_size>;

    /// @brief Writes a wrapper's KNXnet/IP header and security header.
    /// @param destination The octets to write into; holds @p total_length octets.
    /// @param fields The session id, sequence information, serial number and message tag.
    /// @param total_length The wrapper's length, which its header states.
    void write_wrapper_prefix(span_uint8_t destination, const wrapper_fields& fields, std::size_t total_length) noexcept;

    /// @brief Returns the KNXnet/IP header every TIMER_NOTIFY carries.
    [[nodiscard]] timer_notify_header_t timer_notify_header() noexcept;

    /// @brief @ref kmx::aio::knx::secure::seal_wrapper over @p with.
    [[nodiscard]] expected_size_t basic_seal_wrapper(const cipher& with, span_uint8_t destination, const wrapper_fields& fields,
                                                     cspan_uint8_t plain_frame) noexcept;

    /// @brief @ref kmx::aio::knx::secure::open_wrapper over @p with.
    [[nodiscard]] expected_size_t basic_open_wrapper(const cipher& with, span_uint8_t destination, const wrapper_frame& value) noexcept;

    /// @brief @ref kmx::aio::knx::secure::make_timer_notify over @p with, whose key is the backbone key.
    [[nodiscard]] timer_notify_result_t basic_make_timer_notify(const cipher& with, std::uint64_t timer_value,
                                                                const serial_number_t& serial_number,
                                                                const message_tag_t& message_tag) noexcept;

    /// @brief @ref kmx::aio::knx::secure::verify_timer_notify over @p with, whose key is the backbone key.
    [[nodiscard]] expected_void_t basic_verify_timer_notify(const cipher& with, const timer_notify_frame& value) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
