/// @file api/kmx/aio/knx/keyring/document.hpp
/// @brief A loaded ETS keyring's contents: its backbone, interfaces, Data Secure group keys and devices.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// Decrypted keys are held in @ref kmx::aio::knx::secure::secret_key and passwords in @ref
/// kmx::aio::knx::secure::secret_string, which wipe themselves. Passwords are returned as text rather than
/// as derived keys: deriving a key costs 65 536 PBKDF2 iterations, and a keyring with fifty tunnels should not
/// pay for a hundred derivations nobody asked for. @ref kmx::aio::knx::keyring::credentials_for derives the two a
/// connection needs, when that connection is configured.
/// @reference xknx 3.20.0 `xknx/secure/keyring.py`; ETS 5 and 6 keyring exports.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/ipv4.hpp>
        #include <kmx/aio/knx/group_address.hpp>
        #include <kmx/aio/knx/individual_address.hpp>
        #include <kmx/aio/knx/secure/key.hpp>
        #include <kmx/aio/knx/secure/secret_string.hpp>

        #include <cstdint>
        #include <optional>
        #include <string>
        #include <vector>
    #endif

namespace kmx::aio::knx::keyring
{
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
}
#endif // KMX_AIO_FEATURE_KNX
