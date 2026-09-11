/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/entropy.hpp>
#include <kmx/aio/knx/secure/key.hpp>

#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/secure/detail/ccm.hpp>
#include <kmx/aio/knx/secure/detail/crypto.hpp>

namespace kmx::aio::knx::secure
{
    static constexpr std::string_view user_password_salt = "user-password.1.secure.ip.knx.org";
    static constexpr std::string_view device_authentication_salt = "device-authentication-code.1.secure.ip.knx.org";
    static constexpr std::string_view keyring_password_salt = "1.keyring.ets.knx.org";

    secret_string::secret_string(const std::string_view text) noexcept(false): text_(text.begin(), text.end())
    {
    }

    secret_string& secret_string::operator=(secret_string&& other) noexcept
    {
        if (this != &other)
        {
            clear();
            text_ = std::move(other.text_);
            other.text_.clear();
        }
        return *this;
    }

    secret_string::~secret_string() noexcept
    {
        clear();
    }

    void secret_string::clear() noexcept
    {
        detail::cleanse(span_uint8_t {reinterpret_cast<std::uint8_t*>(text_.data()), text_.size()});
        text_.clear();
    }

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

    /// @brief The entropy source backed by the project's TLS backend.
    class system_entropy_source final: public entropy_source
    {
    public:
        [[nodiscard]] expected_void_t fill(const span_uint8_t destination) noexcept override
        {
            if (!detail::evp_backend().random(destination))
                return std::unexpected(make_error_code(error::crypto_failure));
            return {};
        }

        [[nodiscard]] x25519_key_pair_result_t generate_key_pair() noexcept override
        {
            // X25519 clamps the scalar itself, so thirty-two uniformly random octets are a private key as they
            // stand; the public half then follows from the same call a fixed test key goes through.
            x25519_key_pair pair {};
            const auto& backend = detail::evp_backend();
            if (!backend.random(pair.private_key.mutable_bytes()) || !backend.x25519_public(pair.private_key.bytes(), pair.public_key))
                return std::unexpected(make_error_code(error::crypto_failure));
            return pair;
        }
    };

    entropy_source& system_entropy() noexcept
    {
        static system_entropy_source instance {};
        return instance;
    }

    x25519_public_key_result_t derive_x25519_public_key(const x25519_private_key& private_key) noexcept
    {
        x25519_public_key_t public_key {};
        if (!detail::evp_backend().x25519_public(private_key.bytes(), public_key))
            return std::unexpected(make_error_code(error::crypto_failure));
        return public_key;
    }
}
