/// @file inc/kmx/aio/someip/vsomeip_compat/detail/conversions.hpp
/// @brief Conversions between the SOME/IP facade's identifiers and payload bytes and their vsomeip counterparts.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/someip/types.hpp>
    #include <kmx/aio/someip/vsomeip_compat.hpp>

    #include <cstdint>
    #include <memory>
    #include <vector>
#endif

#if defined(KMX_AIO_HAS_VSOMEIP_HEADER)
namespace kmx::aio::someip::vsomeip_compat::detail
{
    /// @brief Packs a service and an instance identifier into one lookup key.
    /// @param service_id The service identifier, stored in the upper 16 bits.
    /// @param instance_id The instance identifier, stored in the lower 16 bits.
    /// @return The combined key.
    [[nodiscard]] std::uint32_t make_service_key(service_id_t service_id, instance_id_t instance_id) noexcept;

    /// @brief Packs a vsomeip client and session identifier into one request identifier.
    /// @param client The client identifier, stored in the upper 16 bits.
    /// @param session The session identifier, stored in the lower 16 bits.
    /// @return The combined request identifier.
    [[nodiscard]] request_id_t make_request_id(vsomeip::client_t client, vsomeip::session_t session) noexcept;

    /// @brief Extracts the vsomeip client identifier from a request identifier.
    /// @param request_id A request identifier built by @ref make_request_id.
    /// @return Its client identifier.
    [[nodiscard]] vsomeip::client_t client_from_request_id(request_id_t request_id) noexcept;

    /// @brief Extracts the vsomeip session identifier from a request identifier.
    /// @param request_id A request identifier built by @ref make_request_id.
    /// @return Its session identifier.
    [[nodiscard]] vsomeip::session_t session_from_request_id(request_id_t request_id) noexcept;

    /// @brief Copies the bytes of a vsomeip payload.
    /// @param payload The payload to copy, or null.
    /// @return Its bytes, or no bytes for a null payload.
    [[nodiscard]] std::vector<std::uint8_t> payload_to_vector(const std::shared_ptr<vsomeip::payload>& payload);

    /// @brief Creates a vsomeip payload holding a copy of some bytes.
    /// @param bytes The bytes to copy.
    /// @return The new payload.
    [[nodiscard]] std::shared_ptr<vsomeip::payload> vector_to_payload(const std::vector<std::uint8_t>& bytes);
}
#endif
