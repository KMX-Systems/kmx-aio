/// @file src/kmx/aio/quic/byte_buffer.cpp
/// @brief Cursor advance and compaction of the QUIC stream byte queue.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#if defined(KMX_AIO_FEATURE_QUIC)
    #include <kmx/aio/quic/byte_buffer.hpp>
    #ifndef PCH
        #include <cstddef>
    #endif

namespace kmx::aio::quic
{
    void byte_buffer::consume(const std::size_t count) noexcept
    {
        read_pos_ += count;
        if (read_pos_ == data_.size())
        {
            // Everything taken, so start again at the front rather than compacting. The capacity stays,
            // which is what makes a stream read to exhaustion and refilled cost no allocation at all.
            data_.clear();
            read_pos_ = 0u;
        }
        else if ((read_pos_ * 2u) >= data_.size())
        {
            data_.erase(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(read_pos_));
            read_pos_ = 0u;
        }
    }
}

#endif // KMX_AIO_FEATURE_QUIC
