/// @file aio/knx/keyring.hpp
/// @brief Reader for ETS keyring exports (`.knxkeys`): backbone, tunnel, group and device keys.
/// @details
/// ETS exports a project's KNX Secure key material as a keyring: an XML document that signs itself and
/// encrypts every key and password it holds under a password the user chooses. @ref
/// kmx::aio::knx::keyring::load checks the signature first - a wrong password and an altered document fail
/// the same way, before anything is decrypted - then decrypts and returns the typed contents.
///
/// Decrypted keys are held in @ref kmx::aio::knx::secure::secret_key and passwords in @ref
/// kmx::aio::knx::secure::secret_string, which wipe themselves. Passwords are returned as text rather than
/// as derived keys: deriving a key costs 65 536 PBKDF2 iterations, and a keyring with fifty tunnels should not
/// pay for a hundred derivations nobody asked for. @ref kmx::aio::knx::keyring::credentials_for derives the two a
/// connection needs, when that connection is configured.
/// @reference xknx 3.20.0 `xknx/secure/keyring.py`; ETS 5 and 6 keyring exports.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <optional>
        #include <string>
        #include <string_view>
        #include <system_error>
        #include <vector>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/ipv4.hpp>
    #include <kmx/aio/knx/address.hpp>
    #include <kmx/aio/knx/secure/credentials.hpp>
    #include <kmx/aio/knx/secure/key.hpp>
    #include <kmx/aio/knx/secure/server_configuration.hpp>

namespace kmx::aio::knx::keyring
{
    /// @brief Largest keyring document accepted, in octets.
    /// @details One mebibyte is far above any real export; the bound keeps a file of attacker-chosen size
    ///          from turning a load into an unbounded one.
    inline constexpr std::size_t max_document_size = 1u << 20u;

    /// @brief What an `Interface` element describes.
    enum class interface_type : std::uint8_t
    {
        /// @brief A KNXnet/IP tunnelling slot (`Tunneling`).
        tunnelling,
        /// @brief A USB interface (`USB`).
        usb,
        /// @brief A backbone interface (`Backbone`).
        backbone,
    };

    /// @brief A group address an interface may receive, and the individual addresses allowed to send to it.
    struct group_senders
    {
        /// @brief The group address.
        group_address address {};
        /// @brief The senders; empty when the keyring names none.
        std::vector<individual_address> senders {};
    };

    /// @brief The secure routing backbone.
    struct backbone
    {
        /// @brief The routing multicast group.
        ipv4::storage_t multicast_address {224u, 0u, 23u, 12u};
        /// @brief The latency tolerance, in milliseconds.
        std::uint16_t latency_ms = 1000u;
        /// @brief The backbone key.
        secure::secret_key key {};
    };

    /// @brief One `Interface` element: a tunnelling slot, a USB interface or a backbone interface.
    struct interface_entry
    {
        /// @brief What the interface is.
        interface_type type = interface_type::tunnelling;
        /// @brief The interface's individual address - a tunnel's own address, for a tunnelling slot.
        individual_address address {};
        /// @brief The device hosting the interface, when the keyring names one.
        std::optional<individual_address> host {};
        /// @brief The KNX IP Secure user id of a tunnelling slot, when it has one.
        std::optional<std::uint8_t> user_id {};
        /// @brief The tunnelling user password; empty when none.
        secure::secret_string user_password {};
        /// @brief The host's device authentication password; empty when none.
        secure::secret_string device_authentication {};
        /// @brief The group addresses the interface receives, with their allowed senders.
        std::vector<group_senders> groups {};
    };

    /// @brief One Data Secure group key.
    struct group_key
    {
        /// @brief The group address.
        group_address address {};
        /// @brief Its key.
        secure::secret_key key {};
    };

    /// @brief One `Device` element.
    struct device
    {
        /// @brief The device's individual address.
        individual_address address {};
        /// @brief The device's tool key; all zero when the keyring carries none.
        secure::secret_key tool_key {};
        /// @brief The device management password; empty when none.
        secure::secret_string management_password {};
        /// @brief The device authentication password; empty when none.
        secure::secret_string authentication {};
        /// @brief The last Data Secure sequence number ETS recorded for the device.
        std::uint64_t sequence_number {};
    };

    /// @brief A loaded, verified and decrypted keyring.
    struct document
    {
        /// @brief The project name.
        std::string project {};
        /// @brief The tool that created the keyring.
        std::string created_by {};
        /// @brief The creation timestamp, which the keyring's IV is derived from.
        std::string created {};
        /// @brief The secure routing backbone, when the project has one.
        std::optional<backbone> backbone_entry {};
        /// @brief Every interface, in document order.
        std::vector<interface_entry> interfaces {};
        /// @brief Every Data Secure group key, in document order.
        std::vector<group_key> group_keys {};
        /// @brief Every device, in document order.
        std::vector<device> devices {};

        /// @brief Finds an interface by its individual address.
        /// @param address The address.
        /// @return The interface, or null.
        [[nodiscard]] const interface_entry* find_interface(individual_address address) const noexcept;
        /// @brief Finds a device by its individual address.
        /// @param address The address.
        /// @return The device, or null.
        [[nodiscard]] const device* find_device(individual_address address) const noexcept;
        /// @brief Finds a group key by its group address.
        /// @param address The address.
        /// @return The key entry, or null.
        [[nodiscard]] const group_key* find_group_key(group_address address) const noexcept;
    };

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
    [[nodiscard]] secure::server_configuration_result_t server_configuration_for(const document& value, individual_address host,
                                                                                 const secure::serial_number_t& serial_number) noexcept(false);
}
#endif // KMX_AIO_FEATURE_KNX
