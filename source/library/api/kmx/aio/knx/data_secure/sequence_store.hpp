/// @file api/kmx/aio/knx/data_secure/sequence_store.hpp
/// @brief Where a KNX Data Secure sender's outgoing sequence numbers are kept across restarts.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>

        #include <cstdint>
        #include <expected>
        #include <system_error>
    #endif

namespace kmx::aio::knx::data_secure
{
    /// @brief Where a sender's outgoing sequence numbers are kept across restarts.
    class sequence_store
    {
    public:
        sequence_store() noexcept = default;
        sequence_store(const sequence_store&) = delete;
        sequence_store& operator=(const sequence_store&) = delete;
        virtual ~sequence_store() noexcept = default;

        /// @brief Returns the first sequence number not yet reserved.
        [[nodiscard]] virtual std::expected<std::uint64_t, std::error_code> load() noexcept = 0;

        /// @brief Durably records that every sequence number below @p limit may be used.
        [[nodiscard]] virtual expected_void_t reserve_until(std::uint64_t limit) noexcept = 0;
    };
}
#endif // KMX_AIO_FEATURE_KNX
