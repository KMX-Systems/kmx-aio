/// @file api/kmx/aio/knx/secure/secret_string.hpp
/// @brief A decrypted KNX Secure password, held in a buffer that is wiped when destroyed or moved from.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <string_view>
        #include <vector>
    #endif

namespace kmx::aio::knx::secure
{
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
}
#endif // KMX_AIO_FEATURE_KNX
