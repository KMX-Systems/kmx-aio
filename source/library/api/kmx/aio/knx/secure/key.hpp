/// @file aio/knx/secure/key.hpp
/// @brief Key material for KNX Secure: fixed-size secrets that wipe themselves, and the password derivations.
/// @details
/// Every symmetric key KNX Secure uses - a session key, a backbone key, a group key, the codes derived from a
/// password - is sixteen octets, and an X25519 private key is thirty-two. They are held in
/// @ref kmx::aio::knx::secure::secret_bytes, which zeroes its octets when it is destroyed or moved from and
/// cannot be copied by accident, so a key does not linger in memory the library has finished with.
///
/// Wiping is best effort. The cryptographic backend makes its own short-lived copies, and a key copied into
/// a coroutine frame lives in heap memory nothing here zeroes, so code that awaits while it needs a key holds
/// the key by reference to a longer-lived owner.
/// @reference KNX System Specifications, 03/08/09 "KNXnet/IP Security", key derivation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <algorithm>
        #include <array>
        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <span>
        #include <string_view>
        #include <system_error>
        #include <vector>
    #endif

    #include <kmx/aio/basic_types.hpp>

namespace kmx::aio::knx::secure
{
    namespace detail
    {
        /// @brief Overwrites octets with zeros in a way the optimiser may not remove.
        /// @param octets The octets to wipe.
        void cleanse(span_uint8_t octets) noexcept;
    }

    /// @brief A fixed-size secret that is wiped when destroyed or moved from, and never copied implicitly.
    /// @tparam Size The number of octets the secret holds.
    template <std::size_t Size>
    class secret_bytes final
    {
    public:
        /// @brief The number of octets the secret holds.
        static constexpr std::size_t size = Size;

        /// @brief Creates an all-zero secret.
        secret_bytes() noexcept = default;

        /// @brief Creates a secret from octets the caller holds.
        /// @param octets The octets to copy in; the caller remains responsible for wiping its own copy.
        explicit secret_bytes(const std::span<const std::uint8_t, Size> octets) noexcept
        {
            std::copy(octets.begin(), octets.end(), octets_.begin());
        }

        secret_bytes(const secret_bytes&) = delete;
        secret_bytes& operator=(const secret_bytes&) = delete;

        /// @brief Takes another secret's octets and wipes the other.
        /// @param other The secret to move from; all zero afterwards.
        secret_bytes(secret_bytes&& other) noexcept: octets_(other.octets_) { other.clear(); }

        /// @brief Takes another secret's octets and wipes the other.
        /// @param other The secret to move from; all zero afterwards.
        /// @return This secret.
        secret_bytes& operator=(secret_bytes&& other) noexcept
        {
            if (this != &other)
            {
                octets_ = other.octets_;
                other.clear();
            }
            return *this;
        }

        /// @brief Wipes the octets.
        ~secret_bytes() noexcept { clear(); }

        /// @brief Returns an explicit copy, for the rare owner that really needs a second one.
        [[nodiscard]] secret_bytes clone() const noexcept { return secret_bytes {bytes()}; }

        /// @brief Indicates whether every octet is zero, which is what an unset secret looks like.
        [[nodiscard]] bool empty() const noexcept
        {
            return std::all_of(octets_.begin(), octets_.end(), [](const std::uint8_t octet) noexcept { return octet == 0u; });
        }

        /// @brief Returns the octets, for handing to a cipher.
        /// @warning Never log them, and never copy them anywhere this type does not wipe.
        [[nodiscard]] std::span<const std::uint8_t, Size> bytes() const noexcept { return octets_; }

        /// @brief Returns the octets for writing, for code that fills a secret in place.
        [[nodiscard]] std::span<std::uint8_t, Size> mutable_bytes() noexcept { return octets_; }

        /// @brief Zeroes the octets now, rather than at destruction.
        void clear() noexcept { detail::cleanse(octets_); }

    private:
        std::array<std::uint8_t, Size> octets_ {};
    };

    /// @brief Width of every symmetric KNX Secure key, in octets.
    inline constexpr std::size_t key_size = 16u;
    /// @brief A 16-octet AES-128 key.
    using secret_key = secret_bytes<key_size>;
    /// @brief A secret key, or the error explaining why none was produced.
    using secret_key_result_t = std::expected<secret_key, std::error_code>;

    /// @brief Width of an X25519 key, private or public, in octets.
    inline constexpr std::size_t x25519_key_size = 32u;
    /// @brief An X25519 private key.
    using x25519_private_key = secret_bytes<x25519_key_size>;
    /// @brief An X25519 public key; public, so an ordinary array.
    using x25519_public_key_t = std::array<std::uint8_t, x25519_key_size>;

    /// @brief One X25519 key pair.
    struct x25519_key_pair
    {
        /// @brief The private half, wiped with the pair.
        x25519_private_key private_key {};
        /// @brief The public half, sent to the peer.
        x25519_public_key_t public_key {};
    };

    /// @brief A key pair, or the error explaining why none was produced.
    using x25519_key_pair_result_t = std::expected<x25519_key_pair, std::error_code>;

    /// @brief A decrypted password, wiped when destroyed or moved from.
    /// @details Held as a buffer of exactly the password's length, so the text is never reallocated into a
    ///          copy this type cannot reach.
    class secret_string final
    {
    public:
        /// @brief Creates an empty secret.
        secret_string() noexcept = default;
        /// @brief Creates a secret from text the caller holds.
        /// @param text The text to copy in.
        /// @throws std::bad_alloc when the buffer cannot be allocated.
        explicit secret_string(std::string_view text) noexcept(false);
        secret_string(const secret_string&) = delete;
        secret_string& operator=(const secret_string&) = delete;
        /// @brief Takes another secret's buffer; the other is left empty.
        /// @param other The secret to move from.
        secret_string(secret_string&& other) noexcept = default;
        /// @brief Wipes this secret, then takes another's buffer.
        /// @param other The secret to move from.
        /// @return This secret.
        secret_string& operator=(secret_string&& other) noexcept;
        /// @brief Wipes the text.
        ~secret_string() noexcept;

        /// @brief Returns the text.
        /// @warning Never log it.
        [[nodiscard]] std::string_view view() const noexcept { return {text_.data(), text_.size()}; }
        /// @brief Indicates whether the text is empty.
        [[nodiscard]] bool empty() const noexcept { return text_.empty(); }

    private:
        void clear() noexcept;

        std::vector<char> text_ {};
    };

    /// @brief Derives the key a KNX IP Secure user password stands for.
    /// @param password The password octets, as the user typed them.
    /// @return The 16-octet key, or @ref kmx::aio::knx::error::crypto_failure.
    /// @details PBKDF2-HMAC-SHA256 over the salt `user-password.1.secure.ip.knx.org`, 65 536 iterations. That
    ///          is tens of milliseconds of work: call it when configuration is built, never per frame.
    [[nodiscard]] secret_key_result_t derive_user_password_key(std::string_view password) noexcept;

    /// @brief Derives a KNX IP Secure device authentication code from its password.
    /// @param password The device authentication password octets.
    /// @return The 16-octet code, or @ref kmx::aio::knx::error::crypto_failure.
    /// @details PBKDF2-HMAC-SHA256 over the salt `device-authentication-code.1.secure.ip.knx.org`, 65 536
    ///          iterations.
    [[nodiscard]] secret_key_result_t derive_device_authentication_code(std::string_view password) noexcept;

    /// @brief Derives the key an ETS keyring's password stands for.
    /// @param password The keyring password octets.
    /// @return The 16-octet hash, or @ref kmx::aio::knx::error::crypto_failure.
    /// @details PBKDF2-HMAC-SHA256 over the salt `1.keyring.ets.knx.org`, 65 536 iterations.
    [[nodiscard]] secret_key_result_t derive_keyring_password_hash(std::string_view password) noexcept;
}
#endif // KMX_AIO_FEATURE_KNX
