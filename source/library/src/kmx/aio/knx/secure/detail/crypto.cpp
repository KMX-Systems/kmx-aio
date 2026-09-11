/// @file src/kmx/aio/knx/secure/detail/crypto.cpp
/// @brief OpenSSL/BoringSSL EVP backend for KNX Secure: AES-128, SHA-256, PBKDF2, X25519 and random bytes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/detail/crypto.hpp>
#ifndef PCH
    #include <kmx/aio/knx/secure/detail/openssl_deleter.hpp>
    #include <kmx/aio/knx/secure/secret_bytes.hpp>

    #include <openssl/crypto.h>
    #include <openssl/evp.h>
    #include <openssl/rand.h>

    #include <algorithm>
    #include <array>
    #include <memory>
#endif

namespace kmx::aio::knx::secure::detail
{
    // OpenSSL and BoringSSL agree on these calls but not on the integer types of their lengths: OpenSSL takes
    // int where BoringSSL takes size_t or uint32_t. Naming the type once keeps every call free of a
    // conversion either compiler would warn about.
#if defined(OPENSSL_IS_BORINGSSL)
    using backend_length_t = std::size_t;
    using backend_iterations_t = std::uint32_t;
#else
    using backend_length_t = int;
    using backend_iterations_t = int;
#endif

    static constexpr std::size_t x25519_size = 32u;

    using cipher_context_t = std::unique_ptr<EVP_CIPHER_CTX, detail::openssl_deleter<&EVP_CIPHER_CTX_free>>;
    using backend_key_t = std::unique_ptr<EVP_PKEY, detail::openssl_deleter<&EVP_PKEY_free>>;
    using key_context_t = std::unique_ptr<EVP_PKEY_CTX, detail::openssl_deleter<&EVP_PKEY_CTX_free>>;

    /// @brief Opens an AES-128 cipher context with padding disabled.
    [[nodiscard]] static cipher_context_t open_cipher(const EVP_CIPHER* const cipher, const cspan_uint8_t key, const cspan_uint8_t iv,
                                                      const bool encrypt) noexcept
    {
        if ((key.size() != aes_block_size) || (iv.size() != aes_block_size))
            return {};
        cipher_context_t context {EVP_CIPHER_CTX_new()};
        if (!context)
            return {};
        const auto initialised = encrypt ? EVP_EncryptInit_ex(context.get(), cipher, nullptr, key.data(), iv.data()) :
                                           EVP_DecryptInit_ex(context.get(), cipher, nullptr, key.data(), iv.data());
        if ((initialised != 1) || (EVP_CIPHER_CTX_set_padding(context.get(), 0) != 1))
            return {};
        return context;
    }

    [[nodiscard]] static bool evp_cbc_mac(const cspan_uint8_t key, const cspan_uint8_t blocks, const span_uint8_t last_block) noexcept
    {
        if (blocks.empty() || ((blocks.size() % aes_block_size) != 0u) || (last_block.size() != aes_block_size))
            return false;
        const std::array<std::uint8_t, aes_block_size> zero_iv {};
        const auto context = open_cipher(EVP_aes_128_cbc(), key, zero_iv, true);
        if (!context)
            return false;

        // Fed one block at a time so the output never needs a buffer the size of the input: only the last
        // ciphertext block is the MAC, and CBC keeps its chaining state inside the context.
        std::array<std::uint8_t, 2u * aes_block_size> output {};
        for (std::size_t offset {}; offset < blocks.size(); offset += aes_block_size)
        {
            int written {};
            if ((EVP_EncryptUpdate(context.get(), output.data(), &written, blocks.data() + offset, static_cast<int>(aes_block_size)) !=
                 1) ||
                (written != static_cast<int>(aes_block_size)))
                return false;
        }

        std::copy_n(output.begin(), aes_block_size, last_block.begin());
        cleanse(output);
        return true;
    }

    /// @brief Runs one in-place CTR update; an empty run needs no call.
    [[nodiscard]] static bool crypt_in_place(EVP_CIPHER_CTX* const context, const span_uint8_t data) noexcept
    {
        if (data.empty())
            return true;
        int written {};
        return (EVP_EncryptUpdate(context, data.data(), &written, data.data(), static_cast<int>(data.size())) == 1) &&
               (written == static_cast<int>(data.size()));
    }

    [[nodiscard]] static bool evp_ctr(const cspan_uint8_t key, const cspan_uint8_t counter_0, const span_uint8_t first,
                                      const span_uint8_t second) noexcept
    {
        const auto context = open_cipher(EVP_aes_128_ctr(), key, counter_0, true);
        return context && crypt_in_place(context.get(), first) && crypt_in_place(context.get(), second);
    }

    [[nodiscard]] static bool evp_cbc_decrypt(const cspan_uint8_t key, const cspan_uint8_t iv, const cspan_uint8_t input,
                                              const span_uint8_t output) noexcept
    {
        if (input.empty() || ((input.size() % aes_block_size) != 0u) || (output.size() < input.size()))
            return false;
        const auto context = open_cipher(EVP_aes_128_cbc(), key, iv, false);
        if (!context)
            return false;
        int written {};
        int finished {};
        return (EVP_DecryptUpdate(context.get(), output.data(), &written, input.data(), static_cast<int>(input.size())) == 1) &&
               (EVP_DecryptFinal_ex(context.get(), output.data() + written, &finished) == 1) &&
               ((written + finished) == static_cast<int>(input.size()));
    }

    [[nodiscard]] static bool evp_sha256(const cspan_uint8_t input, const span_uint8_t digest) noexcept
    {
        if (digest.size() != sha256_size)
            return false;
        unsigned int length {};
        return (EVP_Digest(input.data(), input.size(), digest.data(), &length, EVP_sha256(), nullptr) == 1) && (length == sha256_size);
    }

    [[nodiscard]] static bool evp_pbkdf2_sha256(const cspan_uint8_t password, const cspan_uint8_t salt, const std::uint32_t iterations,
                                                const span_uint8_t output) noexcept
    {
        if (output.empty() || (iterations == 0u))
            return false;
        const auto* const text = reinterpret_cast<const char*>(password.data());
        return PKCS5_PBKDF2_HMAC(text, static_cast<backend_length_t>(password.size()), salt.data(),
                                 static_cast<backend_length_t>(salt.size()), static_cast<backend_iterations_t>(iterations), EVP_sha256(),
                                 static_cast<backend_length_t>(output.size()), output.data()) == 1;
    }

    [[nodiscard]] static bool evp_random(const span_uint8_t output) noexcept
    {
        return output.empty() || (RAND_bytes(output.data(), static_cast<backend_length_t>(output.size())) == 1);
    }

    [[nodiscard]] static bool evp_x25519_public(const cspan_uint8_t private_key, const span_uint8_t public_key) noexcept
    {
        if ((private_key.size() != x25519_size) || (public_key.size() != x25519_size))
            return false;
        const backend_key_t key {EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, private_key.data(), private_key.size())};
        std::size_t length = public_key.size();
        return key && (EVP_PKEY_get_raw_public_key(key.get(), public_key.data(), &length) == 1) && (length == x25519_size);
    }

    [[nodiscard]] static bool evp_x25519_derive(const cspan_uint8_t private_key, const cspan_uint8_t peer_public_key,
                                                const span_uint8_t shared) noexcept
    {
        if ((private_key.size() != x25519_size) || (peer_public_key.size() != x25519_size) || (shared.size() != x25519_size))
            return false;
        const backend_key_t own {EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, private_key.data(), private_key.size())};
        const backend_key_t peer {EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peer_public_key.data(), peer_public_key.size())};
        if (!own || !peer)
            return false;
        const key_context_t context {EVP_PKEY_CTX_new(own.get(), nullptr)};
        std::size_t length = shared.size();
        const auto derived = context && (EVP_PKEY_derive_init(context.get()) == 1) &&
                             (EVP_PKEY_derive_set_peer(context.get(), peer.get()) == 1) &&
                             (EVP_PKEY_derive(context.get(), shared.data(), &length) == 1) && (length == x25519_size);
        // An all-zero agreement is what a low-order peer key produces, whatever this side's key: it carries no
        // secret, so it is refused here even where the backend would return it.
        return derived && !std::all_of(shared.begin(), shared.end(), [](const std::uint8_t octet) noexcept { return octet == 0u; });
    }

    const crypto_backend& evp_backend() noexcept
    {
        static constexpr crypto_backend backend {
            .cbc_mac = &evp_cbc_mac,
            .ctr = &evp_ctr,
            .cbc_decrypt = &evp_cbc_decrypt,
            .sha256 = &evp_sha256,
            .pbkdf2_sha256 = &evp_pbkdf2_sha256,
            .random = &evp_random,
            .x25519_public = &evp_x25519_public,
            .x25519_derive = &evp_x25519_derive,
        };
        return backend;
    }

    bool constant_time_equal(const cspan_uint8_t lhs, const cspan_uint8_t rhs) noexcept
    {
        return (lhs.size() == rhs.size()) && (lhs.empty() || (CRYPTO_memcmp(lhs.data(), rhs.data(), lhs.size()) == 0));
    }

    void cleanse(const span_uint8_t octets) noexcept
    {
        if (!octets.empty())
            OPENSSL_cleanse(octets.data(), octets.size());
    }
}
