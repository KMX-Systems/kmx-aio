/// @file src/kmx/aio/knx/secure/key.cpp
/// @brief KNX Secure key material: the password key derivations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/key.hpp>
#ifndef PCH
    #include <kmx/aio/knx/secure/detail/ccm.hpp>
    #include <kmx/aio/knx/secure/detail/crypto.hpp>
#endif

namespace kmx::aio::knx::secure
{
    static constexpr std::string_view user_password_salt = "user-password.1.secure.ip.knx.org";
    static constexpr std::string_view device_authentication_salt = "device-authentication-code.1.secure.ip.knx.org";
    static constexpr std::string_view keyring_password_salt = "1.keyring.ets.knx.org";

    secret_key_result_t derive_user_password_key(const std::string_view password) noexcept
    {
        return detail::derive_password_key(detail::evp_backend(), password, user_password_salt);
    }

    secret_key_result_t derive_device_authentication_code(const std::string_view password) noexcept
    {
        return detail::derive_password_key(detail::evp_backend(), password, device_authentication_salt);
    }

    secret_key_result_t derive_keyring_password_hash(const std::string_view password) noexcept
    {
        return detail::derive_password_key(detail::evp_backend(), password, keyring_password_salt);
    }
}
