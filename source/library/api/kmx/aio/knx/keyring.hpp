/// @file aio/knx/keyring.hpp
/// @brief Bounded scanner for checked-in KNX key material fragments.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstddef>
    #include <expected>
    #include <span>
    #include <string_view>
#endif

#include <kmx/aio/knx/error.hpp>

namespace kmx::aio::knx::keyring
{
    inline constexpr std::size_t max_document_size = 1u << 20u;
    inline constexpr std::size_t key_size = 16u;

    struct key_record
    {
        std::array<std::uint8_t, key_size> key {};
        std::string_view device_id {};
    };

    class decryptor
    {
    public:
        decryptor() noexcept = default;
        decryptor(const decryptor&) = delete;
        decryptor& operator=(const decryptor&) = delete;
        virtual ~decryptor() noexcept = default;

        [[nodiscard]] virtual std::expected<std::array<std::uint8_t, key_size>, error> decrypt_key(
            std::span<const std::uint8_t> encrypted_key,
            std::string_view password_id) noexcept = 0;
    };

    [[nodiscard]] std::expected<key_record, error> parse(
        std::string_view document) noexcept;
    [[nodiscard]] std::expected<key_record, error> parse_selected(
        std::string_view document,
        std::string_view device_id,
        std::string_view key_id = {},
        decryptor* key_decryptor = nullptr) noexcept;
}
