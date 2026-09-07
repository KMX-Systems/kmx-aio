/// @file aio/knx/secure.hpp
/// @brief Capability and policy boundary for KNX Secure profiles.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstdint>
    #include <expected>
    #include <span>
    #include <system_error>
    #include <vector>
#endif

#include <kmx/aio/basic_types.hpp>
#include <kmx/aio/knx/error.hpp>

namespace kmx::aio::knx::secure
{
    inline constexpr std::uint16_t secure_service = 0x0950u;
    inline constexpr std::size_t secure_packet_header_size = 12u;

    enum class profile : std::uint8_t
    {
        none,
        ip_secure,
        data_secure,
    };

    enum class replay_policy : std::uint8_t
    {
        reject,
        accept_within_window,
    };

    struct configuration
    {
        profile selected = profile::none;
        replay_policy replay = replay_policy::reject;
        std::uint32_t replay_window = 0u;
        std::array<std::uint8_t, 16u> key {};
    };

    struct packet
    {
        profile selected = profile::none;
        std::uint64_t sequence = 0u;
        std::vector<std::uint8_t> payload {};
    };

    [[nodiscard]] constexpr std::expected<void, error> validate(
        const configuration& value) noexcept
    {
        if (value.selected == profile::none)
            return {};
        if (value.replay == replay_policy::accept_within_window && value.replay_window == 0u)
            return std::unexpected(error::invalid_configuration);
        if (value.replay == replay_policy::reject && value.replay_window != 0u)
            return std::unexpected(error::invalid_configuration);
        bool has_key = false;
        for (const auto byte: value.key)
            has_key = has_key || (byte != 0u);
        if (!has_key)
            return std::unexpected(error::invalid_configuration);
        return {};
    }

    class replay_window_state
    {
    public:
        constexpr explicit replay_window_state(const std::uint32_t window = 0u) noexcept:
            window_(window) {}

        [[nodiscard]] constexpr bool accept(const std::uint64_t sequence) noexcept
        {
            if (!initialized_)
            {
                initialized_ = true;
                highest_ = sequence;
                seen_ = 1u;
                return true;
            }

            if (sequence > highest_)
            {
                const auto advance = sequence - highest_;
                if (advance >= 64u)
                    seen_ = 1u;
                else
                    seen_ = (seen_ << advance) | 1u;
                highest_ = sequence;
                return true;
            }

            const auto distance = highest_ - sequence;
            if ((window_ == 0u) || (distance >= window_) || (distance >= 64u))
                return false;
            const auto bit = std::uint64_t {1u} << distance;
            if ((seen_ & bit) != 0u)
                return false;
            seen_ |= bit;
            return true;
        }

        [[nodiscard]] constexpr bool initialized() const noexcept { return initialized_; }
        [[nodiscard]] constexpr std::uint64_t highest() const noexcept { return highest_; }

    private:
        std::uint32_t window_ = 0u;
        std::uint64_t highest_ = 0u;
        std::uint64_t seen_ = 0u;
        bool initialized_ = false;
    };

    class provider
    {
    public:
        provider() noexcept = default;
        provider(const provider&) = delete;
        provider& operator=(const provider&) = delete;
        virtual ~provider() noexcept = default;

        [[nodiscard]] virtual std::expected<std::vector<std::uint8_t>, std::error_code> protect(
            std::span<const std::uint8_t> packet, std::uint64_t sequence) noexcept = 0;
        [[nodiscard]] virtual std::expected<std::vector<std::uint8_t>, std::error_code> unprotect(
            std::span<const std::uint8_t> packet, std::uint64_t sequence) noexcept = 0;
    };

    [[nodiscard]] std::expected<void, std::error_code> encode_secure_packet(
        span_uint8_t destination, const packet& value) noexcept;
    [[nodiscard]] std::expected<packet, std::error_code> decode_secure_packet(
        cspan_uint8_t packet_bytes) noexcept;

    [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::error_code> protect_packet(
        provider& crypto,
        profile selected,
        std::span<const std::uint8_t> payload,
        std::uint64_t sequence) noexcept;
    [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::error_code> unprotect_packet(
        provider& crypto,
        profile expected_profile,
        cspan_uint8_t packet_bytes,
        replay_window_state* replay = nullptr) noexcept;
}
