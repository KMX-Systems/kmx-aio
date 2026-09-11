/// @file kmx/aio/knx/secure/detail/wrapper_crypto.hpp
/// @brief The SECURE_WRAPPER and TIMER_NOTIFY cryptography, over an explicit backend.
/// @details The public functions in `wrapper.hpp` and `timer_notify.hpp` call these with the EVP backend. Tests
///          call them with a failing backend, which is how the crypto failure paths are reached without any
///          global switch (§5.2).
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #include <kmx/aio/knx/secure/detail/crypto.hpp>
    #include <kmx/aio/knx/secure/timer_notify.hpp>
    #include <kmx/aio/knx/secure/wrapper.hpp>

namespace kmx::aio::knx::secure::detail
{
    /// @brief @ref kmx::aio::knx::secure::seal_wrapper over @p backend.
    [[nodiscard]] expected_size_t basic_seal_wrapper(const crypto_backend& backend, span_uint8_t destination, const secret_key& key,
                                                     const wrapper_fields& fields, cspan_uint8_t plain_frame) noexcept;

    /// @brief @ref kmx::aio::knx::secure::open_wrapper over @p backend.
    [[nodiscard]] expected_size_t basic_open_wrapper(const crypto_backend& backend, span_uint8_t destination, const secret_key& key,
                                                     const secure_wrapper_frame& value) noexcept;

    /// @brief @ref kmx::aio::knx::secure::make_timer_notify over @p backend.
    [[nodiscard]] timer_notify_result_t basic_make_timer_notify(const crypto_backend& backend, const secret_key& backbone_key,
                                                                std::uint64_t timer_value, const serial_number_t& serial_number,
                                                                const message_tag_t& message_tag) noexcept;

    /// @brief @ref kmx::aio::knx::secure::verify_timer_notify over @p backend.
    [[nodiscard]] expected_void_t basic_verify_timer_notify(const crypto_backend& backend, const secret_key& backbone_key,
                                                            const timer_notify_frame& value) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
