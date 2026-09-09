/// @file aio/knx/keyring.hpp
/// @brief Bounded scanner for checked-in KNX key material fragments.
/// @details
/// ETS exports a project's KNX Secure key material as a keyring document. This reader takes the one thing
/// the secure boundary needs out of it - a key, by device or by key id - and does so without an XML parser:
/// it scans for `<Key .../>` elements and reads their attributes directly.
///
/// That is a deliberate limit, not an unfinished parser. A keyring is key material, so the reader is the
/// part of the KNX surface most worth keeping small: it never resolves an entity, never follows a DOCTYPE,
/// and refuses a document containing either, which closes the entity-expansion and external-entity classes
/// outright rather than relying on a general parser being configured safely. The input is bounded at
/// @ref kmx::aio::knx::keyring::max_document_size for the same reason.
///
/// Decryption is not done here. An encrypted key is handed to a caller-supplied @ref
/// kmx::aio::knx::keyring::decryptor, so the password handling and the cipher stay with the application
/// that owns the credentials.
/// @reference KNX System Specifications, 03/08/09 "KNXnet/IP Security", keyring export.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
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
    #include <kmx/aio/knx/secure.hpp>

namespace kmx::aio::knx::keyring
{
    /// @brief Largest keyring document this reader accepts, in octets.
    /// @details One mebibyte is far above any real ETS export. The bound exists so a caller that hands over
    ///          a file of attacker-chosen size does not turn a scan into an unbounded one.
    inline constexpr std::size_t max_document_size = 1u << 20u;
    /// @brief Width of a KNX Secure key, in octets.
    inline constexpr std::size_t key_size = secure::key_size;

    /// @brief A KNX Secure key, the same type the secure boundary consumes.
    using key_t = secure::key_t;
    /// @brief A decrypted key, or the error explaining why it could not be recovered.
    using key_result_t = std::expected<key_t, error>;

    /// @brief One recovered key and the device the keyring attributed it to.
    /// @warning @ref device_id is a view into the document that was parsed. It is valid only while that
    ///          document is, which is why the key itself is owned and the identifier is not.
    struct key_record
    {
        /// @brief The recovered key, decrypted if it needed to be.
        key_t key {};
        /// @brief The `device-id` attribute of the element the key came from; empty when it carried none.
        std::string_view device_id {};
    };

    /// @brief A parsed key record, or the error explaining why the document could not be read.
    using key_record_result_t = std::expected<key_record, error>;

    /// @brief The caller-supplied cryptography that turns an encrypted keyring entry into a key.
    /// @details Deliberately an injection point rather than an implementation. Recovering an encrypted key
    ///          needs the keyring password, and this library has no business holding one: the application
    ///          that obtained the credential implements this and keeps it.
    class decryptor
    {
    public:
        /// @brief Constructs a decryptor.
        decryptor() noexcept = default;
        decryptor(const decryptor&) = delete;
        decryptor& operator=(const decryptor&) = delete;
        /// @brief Destroys the decryptor.
        virtual ~decryptor() noexcept = default;

        /// @brief Recovers one key from its encrypted keyring blob.
        /// @param encrypted_key The decoded `encrypted-key` octets.
        /// @param password_id The element's `password-id` attribute, naming which password applies; empty
        ///        when the element carried none.
        /// @return The recovered key, or the reason it could not be recovered.
        [[nodiscard]] virtual key_result_t decrypt_key(cspan_uint8_t encrypted_key, std::string_view password_id) noexcept = 0;
    };

    /// @brief Reads the first key the document offers in the clear.
    /// @param document The keyring document.
    /// @return The key record, or the reason none could be read.
    /// @note Equivalent to @ref parse_selected with no selectors and no decryptor, so a document whose keys
    ///       are all encrypted reports @ref kmx::aio::knx::error::secure_unsupported.
    [[nodiscard]] key_record_result_t parse(std::string_view document) noexcept;

    /// @brief Reads one selected key from a keyring document.
    /// @param document The keyring document.
    /// @param device_id Match only elements carrying this `device-id`; empty matches any.
    /// @param key_id Match only elements carrying this `key-id`; empty matches any.
    /// @param key_decryptor The cryptography to apply to an encrypted match; null skips encrypted entries.
    /// @return The first matching key record, or the reason none could be read.
    /// @retval kmx::aio::knx::error::invalid_length The document is empty or exceeds
    ///         @ref max_document_size.
    /// @retval kmx::aio::knx::error::malformed_frame The document declares a DOCTYPE or an entity, carries
    ///         no `<Key` element, or has a `<Key` element this scanner cannot read.
    /// @retval kmx::aio::knx::error::secure_unsupported An element matched but its key is encrypted and no
    ///         @p key_decryptor was supplied.
    /// @details Elements are scanned in document order and the first match wins. A clear `key` attribute is
    ///          preferred over an `encrypted-key` on the same element.
    /// @warning The returned @ref key_record::device_id points into @p document.
    [[nodiscard]] key_record_result_t parse_selected(
        std::string_view document,
        std::string_view device_id,
        std::string_view key_id = {},
        decryptor* key_decryptor = nullptr) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
