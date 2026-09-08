/// @file aio/knx/keyring.hpp
/// @brief Bounded scanner for checked-in KNX key material fragments.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <array>
        #include <cstddef>
        #include <expected>
        #include <span>
        #include <string_view>
    #endif

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/error.hpp>

namespace kmx::aio::knx::keyring
{
    inline constexpr std::size_t max_document_size = 1u << 20u;
    inline constexpr std::size_t key_size = 16u;

    /// @brief A KNX Data Secure key.
    using key_t = std::array<std::uint8_t, key_size>;
    /// @brief A decrypted key, or the error explaining why it could not be recovered.
    using key_result_t = std::expected<key_t, error>;

    struct key_record
    {
        key_t key {};
        std::string_view device_id {};
    };

    /// @brief A parsed key record, or the error explaining why the document could not be read.
    using key_record_result_t = std::expected<key_record, error>;

    class decryptor
    {
    public:
        decryptor() noexcept = default;
        decryptor(const decryptor&) = delete;
        decryptor& operator=(const decryptor&) = delete;
        virtual ~decryptor() noexcept = default;

        [[nodiscard]] virtual key_result_t decrypt_key(cspan_uint8_t encrypted_key, std::string_view password_id) noexcept = 0;
    };

    [[nodiscard]] key_record_result_t parse(std::string_view document) noexcept;
    [[nodiscard]] key_record_result_t parse_selected(
        std::string_view document,
        std::string_view device_id,
        std::string_view key_id = {},
        decryptor* key_decryptor = nullptr) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
