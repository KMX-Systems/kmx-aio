/// @file aio/gpu/stream.hpp
/// @brief GPU stream wrapper for CUDA stream operations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/gpu/basic_types.hpp>
    #include <kmx/aio/gpu/event.hpp>
#endif

namespace kmx::aio::gpu
{
    /// @brief GPU stream wrapper for CUDA stream operations.
    /// @details Provides a lightweight RAII wrapper around CUDA stream creation,
    ///          synchronization, and destruction. Non-copyable, move-only.
    class stream
    {
    public:
        /// @brief Creates a new GPU stream on the current device.
        /// @throws std::system_error if cudaStreamCreate fails.
        stream() noexcept(false);

        /// @brief Destroys the GPU stream (synchronizes if needed).
        ~stream() noexcept;

        /// @brief Non-copyable.
        stream(const stream&) = delete;
        /// @brief Non-copyable.
        stream& operator=(const stream&) = delete;

        /// @brief Move constructor.
        stream(stream&& other) noexcept;

        /// @brief Move assignment.
        stream& operator=(stream&& other) noexcept;

        /// @brief Returns the underlying CUDA stream handle.
        [[nodiscard]] stream_handle handle() const noexcept { return handle_; }

        /// @brief Synchronizes this stream (blocks until all queued work completes).
        /// @throws std::system_error if cudaStreamSynchronize fails.
        void synchronize() noexcept(false);

        /// @brief Records an event on this stream (for async synchronization).
        /// @return A GPU event that fires when all prior work on this stream completes.
        /// @throws std::system_error if cudaEventCreate or cudaEventRecord fails.
        [[nodiscard]] event create_event() noexcept(false);

    private:
        /// @brief The underlying CUDA stream handle, or null after a move.
        stream_handle handle_ {};

        /// @brief Destroys the CUDA stream and clears @ref handle_.
        void destroy() noexcept;
    };

} // namespace kmx::aio::gpu
