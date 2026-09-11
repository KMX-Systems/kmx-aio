/// @file api/kmx/aio/quic/stream_payload.hpp
/// @brief Move-only payload a QUIC engine hands to its stream handler, and the pooled storage behind it.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_QUIC)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/buffer/handle.hpp>

        #include <array>
        #include <cstddef>
    #endif

namespace kmx::aio::quic
{
    inline constexpr std::size_t stream_payload_capacity = 4096u;
    using stream_payload_buffer = std::array<char, stream_payload_capacity>;

    /// @brief Move-only payload view backed by preallocated storage.
    struct stream_payload
    {
        buffer::handle<stream_payload_buffer> storage;
        std::size_t size {};

        [[nodiscard]] span_char_t bytes() noexcept(false) { return {storage->data(), size}; }
        [[nodiscard]] cspan_char_t bytes() const noexcept(false) { return {storage->data(), size}; }
    };
}

#endif // KMX_AIO_FEATURE_QUIC
