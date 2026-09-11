/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/keyring.hpp>

#include <kmx/aio/knx/error.hpp>
#include <kmx/aio/knx/secure/common.hpp>
#include <kmx/aio/knx/secure/detail/keyring_format.hpp>

#include <algorithm>
#include <charconv>
#include <span>
#include <utility>

namespace kmx::aio::knx::keyring
{
    using secure::detail::find_attribute;
    using secure::detail::xml_event;
    using secure::detail::xml_events_t;

    /// @brief What decrypting a keyring value needs.
    struct decrypt_context
    {
        const secure::detail::crypto_backend& backend;
        const secure::secret_key& password_hash;
        secure::detail::block_t iv {};
    };

    [[nodiscard]] static std::unexpected<std::error_code> malformed() noexcept
    {
        return std::unexpected(make_error_code(error::malformed_frame));
    }

    /// @brief Parses a decimal number no greater than @p maximum.
    template <typename Unsigned>
    [[nodiscard]] static std::optional<Unsigned> parse_unsigned(const std::string_view text, const Unsigned maximum) noexcept
    {
        Unsigned value {};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || (parsed.ec != std::errc {}) || (parsed.ptr != (text.data() + text.size())) || (value > maximum))
            return {};
        return value;
    }

    /// @brief Reads a required individual address attribute.
    [[nodiscard]] static std::expected<individual_address, std::error_code> read_individual(const xml_event& event,
                                                                                            const std::string_view name) noexcept
    {
        const auto* const text = find_attribute(event, name);
        const auto parsed = (text == nullptr) ? std::expected<individual_address, error> {std::unexpected(error::malformed_frame)} :
                                                individual_address::parse(*text);
        if (!parsed.has_value())
            return malformed();
        return *parsed;
    }

    /// @brief Moves a read value into a list, or reports why it could not be read.
    template <typename Value>
    [[nodiscard]] static expected_void_t append(std::vector<Value>& destination,
                                                std::expected<Value, std::error_code>&& read) noexcept(false)
    {
        if (!read.has_value())
            return std::unexpected(read.error());
        destination.push_back(std::move(*read));
        return {};
    }

    /// @brief Decrypts an optional password attribute into @p destination; an absent attribute leaves it empty.
    [[nodiscard]] static expected_void_t read_password(const xml_event& event, const std::string_view name, const decrypt_context& context,
                                                       secure::secret_string& destination) noexcept(false)
    {
        const auto* const encoded = find_attribute(event, name);
        if (encoded == nullptr)
            return {};
        auto decrypted = secure::detail::decrypt_keyring_password(context.backend, *encoded, context.password_hash, context.iv);
        if (!decrypted.has_value())
            return std::unexpected(decrypted.error());
        destination = std::move(*decrypted);
        return {};
    }

    /// @brief Decrypts a required key attribute into @p destination.
    [[nodiscard]] static expected_void_t read_key(const xml_event& event, const std::string_view name, const decrypt_context& context,
                                                  secure::secret_key& destination) noexcept(false)
    {
        const auto* const encoded = find_attribute(event, name);
        if (encoded == nullptr)
            return malformed();
        auto decrypted = secure::detail::decrypt_keyring_key(context.backend, *encoded, context.password_hash, context.iv);
        if (!decrypted.has_value())
            return std::unexpected(decrypted.error());
        destination = std::move(*decrypted);
        return {};
    }

    [[nodiscard]] static std::expected<backbone, std::error_code> read_backbone(const xml_event& event,
                                                                                const decrypt_context& context) noexcept(false)
    {
        backbone value {};
        if (const auto key = read_key(event, "Key", context, value.key); !key.has_value())
            return std::unexpected(key.error());
        if (const auto* const address = find_attribute(event, "MulticastAddress");
            (address != nullptr) && !ipv4::parse_address(*address, value.multicast_address))
            return malformed();
        if (const auto* const latency = find_attribute(event, "Latency"); latency != nullptr)
        {
            const auto parsed = parse_unsigned<std::uint16_t>(*latency, 0xFFFFu);
            if (!parsed.has_value())
                return malformed();
            value.latency_ms = *parsed;
        }
        return value;
    }

    [[nodiscard]] static std::optional<interface_type> interface_type_of(const std::string_view text) noexcept
    {
        if (text == "Tunneling")
            return interface_type::tunnelling;
        if (text == "USB")
            return interface_type::usb;
        if (text == "Backbone")
            return interface_type::backbone;
        return {};
    }

    /// @brief Reads an interface's type, address, host and user id.
    [[nodiscard]] static expected_void_t read_interface_identity(const xml_event& event, interface_entry& value) noexcept
    {
        const auto* const type = find_attribute(event, "Type");
        const auto parsed_type = (type == nullptr) ? std::optional<interface_type> {} : interface_type_of(*type);
        const auto address = read_individual(event, "IndividualAddress");
        if (!parsed_type.has_value() || !address.has_value())
            return malformed();
        value.type = *parsed_type;
        value.address = *address;
        if (find_attribute(event, "Host") != nullptr)
        {
            const auto host = read_individual(event, "Host");
            if (!host.has_value())
                return malformed();
            value.host = *host;
        }
        if (const auto* const user = find_attribute(event, "UserID"); user != nullptr)
        {
            value.user_id = parse_unsigned<std::uint8_t>(*user, 127u);
            if (!value.user_id.has_value())
                return malformed();
        }
        return {};
    }

    [[nodiscard]] static std::expected<interface_entry, std::error_code> read_interface(const xml_event& event,
                                                                                        const decrypt_context& context) noexcept(false)
    {
        interface_entry value {};
        if (const auto identity = read_interface_identity(event, value); !identity.has_value())
            return std::unexpected(identity.error());
        if (const auto password = read_password(event, "Password", context, value.user_password); !password.has_value())
            return std::unexpected(password.error());
        if (const auto authentication = read_password(event, "Authentication", context, value.device_authentication);
            !authentication.has_value())
            return std::unexpected(authentication.error());
        return value;
    }

    /// @brief Reads a required group address attribute.
    [[nodiscard]] static std::expected<group_address, std::error_code> read_group_address(const xml_event& event) noexcept
    {
        const auto* const text = find_attribute(event, "Address");
        const auto parsed =
            (text == nullptr) ? std::expected<group_address, error> {std::unexpected(error::malformed_frame)} : group_address::parse(*text);
        if (!parsed.has_value())
            return malformed();
        return *parsed;
    }

    [[nodiscard]] static std::expected<group_senders, std::error_code> read_group_senders(const xml_event& event) noexcept(false)
    {
        group_senders value {};
        const auto address = read_group_address(event);
        if (!address.has_value())
            return std::unexpected(address.error());
        value.address = *address;

        const auto* const senders = find_attribute(event, "Senders");
        for (std::string_view rest = (senders == nullptr) ? std::string_view {} : std::string_view {*senders}; !rest.empty();)
        {
            const auto space = rest.find(' ');
            const auto token = rest.substr(0u, space);
            rest = (space == std::string_view::npos) ? std::string_view {} : rest.substr(space + 1u);
            if (token.empty())
                continue;
            const auto sender = individual_address::parse(token);
            if (!sender.has_value())
                return malformed();
            value.senders.push_back(*sender);
        }
        return value;
    }

    [[nodiscard]] static std::expected<group_key, std::error_code> read_group_key(const xml_event& event,
                                                                                  const decrypt_context& context) noexcept(false)
    {
        group_key value {};
        const auto address = read_group_address(event);
        if (!address.has_value())
            return std::unexpected(address.error());
        value.address = *address;
        if (const auto key = read_key(event, "Key", context, value.key); !key.has_value())
            return std::unexpected(key.error());
        return value;
    }

    [[nodiscard]] static std::expected<device, std::error_code> read_device(const xml_event& event,
                                                                            const decrypt_context& context) noexcept(false)
    {
        device value {};
        const auto address = read_individual(event, "IndividualAddress");
        if (!address.has_value())
            return std::unexpected(address.error());
        value.address = *address;
        if (find_attribute(event, "ToolKey") != nullptr)
            if (const auto key = read_key(event, "ToolKey", context, value.tool_key); !key.has_value())
                return std::unexpected(key.error());
        if (const auto password = read_password(event, "ManagementPassword", context, value.management_password); !password.has_value())
            return std::unexpected(password.error());
        if (const auto authentication = read_password(event, "Authentication", context, value.authentication); !authentication.has_value())
            return std::unexpected(authentication.error());
        if (const auto* const sequence = find_attribute(event, "SequenceNumber"); sequence != nullptr)
        {
            const auto parsed = parse_unsigned<std::uint64_t>(*sequence, secure::max_sequence);
            if (!parsed.has_value())
                return malformed();
            value.sequence_number = *parsed;
        }
        return value;
    }

    /// @brief Reads one element below the root, by where it sits; anything unmodelled is signed but ignored.
    [[nodiscard]] static expected_void_t read_element(document& value, const std::span<const std::string_view> path, const xml_event& event,
                                                      const decrypt_context& context) noexcept(false)
    {
        if ((path.size() == 2u) && (path[1u] == "Backbone"))
        {
            auto read = read_backbone(event, context);
            if (!read.has_value())
                return std::unexpected(read.error());
            value.backbone_entry = std::move(*read);
            return {};
        }
        if ((path.size() == 2u) && (path[1u] == "Interface"))
            return append(value.interfaces, read_interface(event, context));
        if ((path.size() == 3u) && (path[1u] == "Interface") && (path[2u] == "Group"))
            return append(value.interfaces.back().groups, read_group_senders(event));
        if ((path.size() == 3u) && (path[1u] == "GroupAddresses") && (path[2u] == "Group"))
            return append(value.group_keys, read_group_key(event, context));
        if ((path.size() == 3u) && (path[1u] == "Devices") && (path[2u] == "Device"))
            return append(value.devices, read_device(event, context));
        return {};
    }

    [[nodiscard]] static document_result_t read_document(const xml_events_t& events, const decrypt_context& context) noexcept(false)
    {
        document value {};
        std::vector<std::string_view> path {};
        for (const auto& event: events)
        {
            if (event.kind == secure::detail::xml_event_kind::end)
            {
                path.pop_back();
                continue;
            }
            path.push_back(event.name);
            if (const auto read = read_element(value, path, event, context); !read.has_value())
                return std::unexpected(read.error());
        }
        return value;
    }

    document_result_t load(const std::string_view xml, const std::string_view password) noexcept(false)
    {
        if (xml.empty() || (xml.size() > max_document_size))
            return std::unexpected(make_error_code(error::invalid_length));
        const auto hash = secure::derive_keyring_password_hash(password);
        if (!hash.has_value())
            return std::unexpected(hash.error());
        return load(xml, *hash);
    }

    document_result_t load(const std::string_view xml, const secure::secret_key& password_hash) noexcept(false)
    {
        if (xml.empty() || (xml.size() > max_document_size))
            return std::unexpected(make_error_code(error::invalid_length));
        const auto events = secure::detail::read_xml(xml);
        if (!events.has_value())
            return std::unexpected(events.error());
        const auto* const created = events->empty() ? nullptr : find_attribute(events->front(), "Created");
        if ((created == nullptr) || (events->front().name != "Keyring"))
            return malformed();

        // The signature is checked before anything is decrypted, so a wrong password and an altered document
        // are reported the same way and neither reaches the decryption below.
        const auto& backend = secure::detail::evp_backend();
        if (const auto verified = secure::detail::verify_keyring_signature(backend, *events, password_hash); !verified.has_value())
            return std::unexpected(verified.error());
        const auto iv = secure::detail::keyring_initialisation_vector(backend, *created);
        if (!iv.has_value())
            return std::unexpected(iv.error());

        auto value = read_document(*events, decrypt_context {backend, password_hash, *iv});
        if (!value.has_value())
            return value;
        const auto* const project = find_attribute(events->front(), "Project");
        const auto* const created_by = find_attribute(events->front(), "CreatedBy");
        value->project = (project == nullptr) ? std::string {} : *project;
        value->created_by = (created_by == nullptr) ? std::string {} : *created_by;
        value->created = *created;
        return value;
    }

    const interface_entry* document::find_interface(const individual_address address) const noexcept
    {
        const auto found = std::ranges::find(interfaces, address, &interface_entry::address);
        return (found == interfaces.end()) ? nullptr : &*found;
    }

    const device* document::find_device(const individual_address address) const noexcept
    {
        const auto found = std::ranges::find(devices, address, &device::address);
        return (found == devices.end()) ? nullptr : &*found;
    }

    const group_key* document::find_group_key(const group_address address) const noexcept
    {
        const auto found = std::ranges::find(group_keys, address, &group_key::address);
        return (found == group_keys.end()) ? nullptr : &*found;
    }

    [[nodiscard]] static std::unexpected<std::error_code> refuse(const error reason) noexcept
    {
        return std::unexpected(make_error_code(reason));
    }

    /// @brief Indicates whether an interface carries everything a secure tunnel needs.
    [[nodiscard]] static bool tunnel_credentials_present(const interface_entry* const tunnel) noexcept
    {
        return (tunnel != nullptr) && (tunnel->type == interface_type::tunnelling) && tunnel->user_id.has_value() &&
               (*tunnel->user_id != 0u) && !tunnel->user_password.empty() && !tunnel->device_authentication.empty();
    }

    secure::tunnelling_credentials_result_t credentials_for(const document& value, const individual_address tunnel_address,
                                                            const secure::serial_number_t& serial_number) noexcept
    {
        // Both refusals come before the derivations, which are where the time goes.
        if (!secure::valid_serial_number(serial_number))
            return refuse(error::invalid_configuration);
        const auto* const tunnel = value.find_interface(tunnel_address);
        if (!tunnel_credentials_present(tunnel))
            return refuse(error::secure_key_missing);

        auto user_password_key = secure::derive_user_password_key(tunnel->user_password.view());
        auto device_authentication_code = user_password_key.has_value() ?
                                              secure::derive_device_authentication_code(tunnel->device_authentication.view()) :
                                              secure::secret_key_result_t {std::unexpected(user_password_key.error())};
        if (!device_authentication_code.has_value())
            return std::unexpected(device_authentication_code.error());

        secure::tunnelling_credentials credentials {};
        credentials.user_id = *tunnel->user_id;
        credentials.user_password_key = std::move(*user_password_key);
        credentials.device_authentication_code = std::move(*device_authentication_code);
        credentials.serial_number = serial_number;
        return credentials;
    }

    secure::routing_configuration_result_t routing_configuration_for(const document& value,
                                                                     const secure::serial_number_t& serial_number) noexcept
    {
        if (!secure::valid_serial_number(serial_number))
            return refuse(error::invalid_configuration);
        if (!value.backbone_entry.has_value())
            return refuse(error::secure_key_missing);

        secure::routing_configuration configuration {};
        configuration.backbone_key = value.backbone_entry->key.clone();
        configuration.multicast_address = value.backbone_entry->multicast_address;
        configuration.latency_tolerance_ms = value.backbone_entry->latency_ms;
        configuration.serial_number = serial_number;
        return configuration;
    }

    /// @brief Returns every tunnelling slot on @p host that carries what a secure tunnel needs, in document order.
    [[nodiscard]] static std::vector<const interface_entry*> hosted_tunnels(const document& value, const individual_address host) noexcept(false)
    {
        std::vector<const interface_entry*> hosted {};
        for (const auto& tunnel: value.interfaces)
        {
            if (tunnel_credentials_present(&tunnel) && tunnel.host.has_value() && (*tunnel.host == host))
                hosted.push_back(&tunnel);
        }
        return hosted;
    }

    /// @brief Adds a tunnelling slot to the server's users: a new user, or one more tunnel address of a user it has.
    [[nodiscard]] static expected_void_t add_tunnel(secure::server_configuration& configuration, const std::span<const interface_entry* const> hosted,
                                                    const interface_entry& tunnel) noexcept(false)
    {
        const auto user = std::ranges::find(configuration.users, *tunnel.user_id, &secure::tunnelling_user::user_id);
        if (user != configuration.users.end())
        {
            // One user id is one password: a second slot under it has to carry the same one.
            const auto first = std::ranges::find_if(hosted, [&tunnel](const interface_entry* const other) noexcept { return other->user_id == tunnel.user_id; });
            if ((*first)->user_password.view() != tunnel.user_password.view())
                return refuse(error::invalid_configuration);
            user->tunnel_addresses.push_back(tunnel.address);
            return {};
        }
        auto password_key = secure::derive_user_password_key(tunnel.user_password.view());
        if (!password_key.has_value())
            return std::unexpected(password_key.error());
        configuration.users.push_back(
            secure::tunnelling_user {.user_id = *tunnel.user_id, .password_key = std::move(*password_key), .tunnel_addresses = {tunnel.address}});
        return {};
    }

    secure::server_configuration_result_t server_configuration_for(const document& value, const individual_address host,
                                                                   const secure::serial_number_t& serial_number) noexcept(false)
    {
        if (!secure::valid_serial_number(serial_number))
            return refuse(error::invalid_configuration);
        const auto hosted = hosted_tunnels(value, host);
        if (hosted.empty())
            return refuse(error::secure_key_missing);
        // One device has one device authentication code; slots that disagree describe no device that can exist.
        const auto& device_code = hosted.front()->device_authentication;
        if (std::ranges::any_of(hosted, [&device_code](const interface_entry* const tunnel) noexcept
                                { return tunnel->device_authentication.view() != device_code.view(); }))
            return refuse(error::invalid_configuration);

        secure::server_configuration configuration {};
        auto derived = secure::derive_device_authentication_code(device_code.view());
        if (!derived.has_value())
            return std::unexpected(derived.error());
        configuration.device_authentication_code = std::move(*derived);
        configuration.serial_number = serial_number;
        for (const auto* const tunnel: hosted)
        {
            if (const auto added = add_tunnel(configuration, hosted, *tunnel); !added.has_value())
                return std::unexpected(added.error());
        }
        return configuration;
    }
}
