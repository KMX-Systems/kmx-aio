/// @file api/kmx/aio/knx/keyring.hpp
/// @brief Reader for ETS keyring exports (`.knxkeys`): backbone, tunnel, group and device keys.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// ETS exports a project's KNX Secure key material as a keyring: an XML document that signs itself and
/// encrypts every key and password it holds under a password the user chooses. @ref
/// kmx::aio::knx::keyring::load checks the signature first - a wrong password and an altered document fail
/// the same way, before anything is decrypted - then decrypts and returns the typed contents.
/// @reference xknx 3.20.0 `xknx/secure/keyring.py`; ETS 5 and 6 keyring exports.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/knx/individual_address.hpp>
        #include <kmx/aio/knx/keyring/document.hpp>
        #include <kmx/aio/knx/secure/common.hpp>
        #include <kmx/aio/knx/secure/credentials.hpp>
        #include <kmx/aio/knx/secure/key.hpp>
        #include <kmx/aio/knx/secure/server_configuration.hpp>

        #include <cstddef>
        #include <expected>
        #include <string_view>
        #include <system_error>
    #endif

namespace kmx::aio::knx::keyring
{
    /// @brief Largest keyring document accepted, in octets.
    /// @details One mebibyte is far above any real export; the bound keeps a file of attacker-chosen size
    ///          from turning a load into an unbounded one.
    inline constexpr std::size_t max_document_size = 1u << 20u;

    /// @brief A loaded keyring, or why it could not be loaded.
    using document_result_t = std::expected<document, std::error_code>;

    /// @brief Loads a keyring, deriving the password hash from the password.
    /// @param xml The keyring document.
    /// @param password The keyring password.
    /// @return The keyring.
    /// @retval kmx::aio::knx::error::invalid_length The document is empty or above @ref max_document_size.
    /// @retval kmx::aio::knx::error::keyring_signature_invalid The password is wrong or the document was altered.
    /// @retval kmx::aio::knx::error::malformed_frame The document is not a keyring this reader accepts.
    /// @throws std::bad_alloc when the contents cannot be stored.
    /// @note Costs one PBKDF2 derivation of 65 536 iterations; use the other overload to load repeatedly.
    [[nodiscard]] document_result_t load(std::string_view xml, std::string_view password) noexcept(false);

    /// @brief Loads a keyring with a password hash already derived by
    ///        @ref kmx::aio::knx::secure::derive_keyring_password_hash.
    /// @param xml The keyring document.
    /// @param password_hash The keyring password hash.
    /// @return The keyring; the failures are as for the other overload.
    /// @throws std::bad_alloc when the contents cannot be stored.
    [[nodiscard]] document_result_t load(std::string_view xml, const secure::secret_key& password_hash) noexcept(false);

    /// @brief Builds the credentials for a secure tunnel to one of the keyring's tunnelling slots.
    /// @param value The keyring.
    /// @param tunnel_address The tunnel's individual address.
    /// @param serial_number This client's KNX serial number.
    /// @return The credentials, both keys derived.
    /// @retval kmx::aio::knx::error::invalid_configuration The serial number is all zero (P8).
    /// @retval kmx::aio::knx::error::secure_key_missing The keyring has no tunnelling slot at that address, or
    ///         the slot lacks a user id, a user password or a device authentication code.
    /// @retval kmx::aio::knx::error::crypto_failure A derivation failed.
    /// @note Runs two PBKDF2 derivations of 65 536 iterations each, so call it when a connection is configured
    ///       rather than on every attempt. Device authentication is never skipped here: a caller that means to
    ///       skip it sets the flag on the result, by name.
    [[nodiscard]] secure::tunnelling_credentials_result_t credentials_for(const document& value, individual_address tunnel_address,
                                                                          const secure::serial_number_t& serial_number) noexcept;

    /// @brief Builds the configuration for joining the keyring's secure routing backbone.
    /// @param value The keyring.
    /// @param serial_number This router's KNX serial number.
    /// @return The configuration, holding its own copy of the backbone key.
    /// @retval kmx::aio::knx::error::invalid_configuration The serial number is all zero (P8).
    /// @retval kmx::aio::knx::error::secure_key_missing The keyring has no backbone.
    [[nodiscard]] secure::routing_configuration_result_t routing_configuration_for(const document& value,
                                                                                   const secure::serial_number_t& serial_number) noexcept;

    /// @brief Builds the configuration for a secure tunnelling server hosting the keyring's tunnelling slots on one device.
    /// @param value The keyring.
    /// @param host The individual address of the device hosting the tunnels. Every tunnelling slot the keyring places on it
    ///        becomes a user, or one more tunnel address of a user it already has.
    /// @param serial_number The server's KNX serial number.
    /// @return The configuration, every password derived, with the default limits.
    /// @retval kmx::aio::knx::error::invalid_configuration The serial number is all zero (P8), the slots on @p host disagree
    ///         on the device authentication code, or two slots under one user id disagree on its password.
    /// @retval kmx::aio::knx::error::secure_key_missing @p host has no tunnelling slot with a user id, a user password and
    ///         a device authentication code.
    /// @retval kmx::aio::knx::error::crypto_failure A derivation failed.
    /// @throws std::bad_alloc when the user table cannot be allocated.
    /// @note Runs one PBKDF2 derivation per user, and one more for the device authentication code.
    [[nodiscard]] secure::server_configuration_result_t server_configuration_for(
        const document& value, individual_address host, const secure::serial_number_t& serial_number) noexcept(false);
}
#endif // KMX_AIO_FEATURE_KNX
