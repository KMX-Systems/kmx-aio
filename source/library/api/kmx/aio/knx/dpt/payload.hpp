/// @file api/kmx/aio/knx/dpt/payload.hpp
/// @brief An encoded KNX datapoint value that owns its octets.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details
/// Values narrower than seven bits are not sent as octets at all: they ride in the six spare bits of the
/// APCI octet. @ref kmx::aio::knx::dpt::payload carries that distinction so it never has to be inferred
/// from a value's width.
/// @reference KNX System Specifications, Volume 3/7/2 "Datapoint Types".
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/knx/apdu_payload.hpp>
        #include <kmx/aio/knx/error.hpp>

        #include <array>
        #include <cstdint>
        #include <expected>
    #endif

namespace kmx::aio::knx::dpt
{
    /// @brief An encoded datapoint value, owning its octets.
    /// @details Fourteen octets is the widest value any implemented main type produces, which is the
    ///          character string of DPT 16. Keeping the storage inline means encoding a telegram allocates
    ///          nothing at all.
    class payload
    {
    public:
        /// @brief The largest number of octets an encoded value occupies.
        static constexpr std::size_t capacity = 14u;

        /// @brief Creates the compact value zero.
        constexpr payload() noexcept = default;

        /// @brief Creates a value that rides in the six spare bits of the APCI octet.
        /// @param value The six-bit value; higher bits are discarded.
        /// @return The payload.
        [[nodiscard]] static constexpr payload compact(const std::uint8_t value) noexcept
        {
            payload result {};
            result.compact_value_ = static_cast<std::uint8_t>(value & apdu_payload::compact_mask);
            return result;
        }

        /// @brief Creates a value carried in whole octets.
        /// @param octets The octets to copy in.
        /// @return The payload, or `error::payload_too_large` when it exceeds @ref capacity.
        [[nodiscard]] static constexpr std::expected<payload, error> octets(const cspan_uint8_t octets) noexcept
        {
            if (octets.size() > capacity)
                return std::unexpected(error::payload_too_large);

            payload result {};
            for (std::size_t i {}; i < octets.size(); ++i)
                result.storage_[i] = octets[i];

            result.size_ = static_cast<std::uint8_t>(octets.size());
            result.compacted_ = false;
            return result;
        }

        /// @brief Indicates whether the value rides in the six spare bits of the APCI octet.
        [[nodiscard]] constexpr bool compacted() const noexcept { return compacted_; }
        /// @brief Returns the six-bit value; zero unless the payload is compact.
        [[nodiscard]] constexpr std::uint8_t compact_value() const noexcept { return compact_value_; }
        /// @brief Returns the payload octets; empty when the payload is compact.
        [[nodiscard]] constexpr cspan_uint8_t view() const noexcept { return {storage_.data(), size_}; }

        /// @brief Returns the payload as an APDU payload the cEMI encoder accepts.
        /// @return A view of this object's storage.
        /// @warning The returned view borrows this object's octets, so this object must outlive it. The
        ///          rvalue overload is deleted to keep that mistake from compiling.
        [[nodiscard]] constexpr apdu_payload apdu() const& noexcept
        {
            if (compacted_)
                return apdu_payload::compact(compact_value_);

            return apdu_payload::extended(view()).value_or(apdu_payload {});
        }

        /// @brief Deleted so an APDU view cannot outlive a temporary payload.
        apdu_payload apdu() const&& = delete;

        /// @brief Compares two payloads by form and content.
        [[nodiscard]] constexpr bool operator==(const payload& other) const noexcept
        {
            if ((compacted_ != other.compacted_) || (compact_value_ != other.compact_value_) || (size_ != other.size_))
                return false;

            for (std::uint8_t i {}; i < size_; ++i)
                if (storage_[i] != other.storage_[i])
                    return false;

            return true;
        }

    private:
        /// @brief The value octets of a non-compact payload.
        std::array<std::uint8_t, capacity> storage_ {};
        /// @brief The number of value octets in use.
        std::uint8_t size_ {};
        /// @brief The six-bit value of a compact payload.
        std::uint8_t compact_value_ {};
        /// @brief Whether the value rides in the APCI octet.
        bool compacted_ {true};
    };
}
#endif // KMX_AIO_FEATURE_KNX
