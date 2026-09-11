/// @file inc/kmx/aio/test/knx/secure_vectors/scripted_entropy.hpp
/// @brief An entropy source for the KNX Secure tests that hands out scripted octets, or fails when told to.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/secure/entropy_source.hpp>
    #include <kmx/aio/knx/secure/key.hpp>

    #include <cstdint>
    #include <deque>
    #include <expected>
#endif

namespace kmx::aio::test::knx::secure_vectors
{
    /// @brief An entropy source that hands out scripted octets and then zeros, or fails when told to.
    /// @details Zeros make every delay the shortest its range allows, which is what most tests reason about.
    class scripted_entropy final: public kmx::aio::knx::secure::entropy_source
    {
    public:
        /// @brief The octets still to hand out.
        std::deque<std::uint8_t> octets {};
        /// @brief Whether every fill fails.
        bool failing {};

        [[nodiscard]] expected_void_t fill(const span_uint8_t destination) noexcept override
        {
            if (failing)
                return std::unexpected(kmx::aio::knx::make_error_code(kmx::aio::knx::error::crypto_failure));
            for (auto& octet: destination)
            {
                octet = octets.empty() ? std::uint8_t {} : octets.front();
                if (!octets.empty())
                    octets.pop_front();
            }

            return {};
        }

        [[nodiscard]] kmx::aio::knx::secure::x25519_key_pair_result_t generate_key_pair() noexcept override
        {
            return std::unexpected(kmx::aio::knx::make_error_code(kmx::aio::knx::error::crypto_failure));
        }

        /// @brief Scripts the next message tag.
        void tag(const std::uint16_t value) noexcept(false)
        {
            octets.push_back(static_cast<std::uint8_t>(value >> 8u));
            octets.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        }

        /// @brief Scripts the next delay draw: @p offset milliseconds above the shortest, modulo the range.
        void delay(const std::uint32_t offset) noexcept(false)
        {
            for (const auto shift: {24u, 16u, 8u, 0u})
                octets.push_back(static_cast<std::uint8_t>((offset >> shift) & 0xFFu));
        }
    };
}
