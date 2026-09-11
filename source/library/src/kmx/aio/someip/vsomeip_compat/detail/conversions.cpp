/// @file src/kmx/aio/someip/vsomeip_compat/detail/conversions.cpp
/// @brief Conversions between the SOME/IP facade's identifiers and payload bytes and their vsomeip counterparts.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/someip/vsomeip_compat/detail/conversions.hpp>

#if defined(KMX_AIO_HAS_VSOMEIP_HEADER)
namespace kmx::aio::someip::vsomeip_compat::detail
{
    [[nodiscard]] std::uint32_t make_service_key(const service_id_t service_id, const instance_id_t instance_id) noexcept
    {
        return (static_cast<std::uint32_t>(service_id) << 16u) | static_cast<std::uint32_t>(instance_id);
    }

    [[nodiscard]] request_id_t make_request_id(const vsomeip::client_t client, const vsomeip::session_t session) noexcept
    {
        return (static_cast<request_id_t>(client) << 16u) | static_cast<request_id_t>(session);
    }

    [[nodiscard]] vsomeip::client_t client_from_request_id(const request_id_t request_id) noexcept
    {
        return static_cast<vsomeip::client_t>((request_id >> 16u) & 0xFFFFu);
    }

    [[nodiscard]] vsomeip::session_t session_from_request_id(const request_id_t request_id) noexcept
    {
        return static_cast<vsomeip::session_t>(request_id & 0xFFFFu);
    }

    [[nodiscard]] std::vector<std::uint8_t> payload_to_vector(const std::shared_ptr<vsomeip::payload>& payload)
    {
        if (payload == nullptr)
            return {};

        const auto* data = payload->get_data();
        const auto length = payload->get_length();
        return {data, data + length};
    }

    [[nodiscard]] std::shared_ptr<vsomeip::payload> vector_to_payload(const std::vector<std::uint8_t>& bytes)
    {
        auto payload = vsomeip::runtime::get()->create_payload();
        payload->set_data(bytes);
        return payload;
    }
}
#endif
