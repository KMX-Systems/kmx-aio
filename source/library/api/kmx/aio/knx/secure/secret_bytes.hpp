/// @file api/kmx/aio/knx/secure/secret_bytes.hpp
/// @brief A fixed-size secret that wipes itself when destroyed or moved from, and the wipe it is built on.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// Wiping is best effort. The cryptographic backend makes its own short-lived copies, and a key copied into
/// a coroutine frame lives in heap memory nothing here zeroes, so code that awaits while it needs a key holds
/// the key by reference to a longer-lived owner.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>

        #include <algorithm>
        #include <array>
        #include <cstddef>
        #include <cstdint>
        #include <span>
    #endif

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
}
#endif // KMX_AIO_FEATURE_KNX
