/// @file api/kmx/aio/readiness/v4l2/frame_view.hpp
/// @brief Zero-copy view of one captured V4L2 frame that re-enqueues its buffer on destruction — readiness (epoll) model.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/readiness/v4l2/v4l2_types.hpp>

        #include <cstddef>
        #include <cstdint>
        #include <memory>
    #endif

namespace kmx::aio::readiness::v4l2
{
    /// @brief Zero-copy view of a single captured frame.
    ///
    /// Wraps the mmap'd kernel buffer for the duration of frame processing.
    /// Automatically re-enqueues the buffer (VIDIOC_QBUF) when destroyed, returning
    /// it to the driver for the next capture cycle.
    ///
    /// @warning The `frame_view` must not outlive the `capture` object that created it.
    ///          Holding a `frame_view` across a co_await that suspends past the capture
    ///          object's destruction is undefined behaviour.
    class frame_view
    {
    public:
        /// @brief A frame view is only ever produced by @ref capture; default construction is disabled.
        frame_view() = delete;
        /// @brief Non-copyable: the view owns a driver buffer slot.
        frame_view(const frame_view&) = delete;
        /// @brief Non-copyable: the view owns a driver buffer slot.
        frame_view& operator=(const frame_view&) = delete;

        /// @brief Move constructor — transfers ownership of the buffer slot.
        frame_view(frame_view&&) noexcept;

        /// @brief Move assignment is disabled to keep ownership unambiguous.
        frame_view& operator=(frame_view&&) noexcept = delete;

        /// @brief Returns the buffer to the driver.
        ~frame_view() noexcept;

        /// @brief Raw frame bytes (zero-copy view into the mmap'd kernel buffer).
        [[nodiscard]] cspan_byte_t data() const noexcept;

        /// @brief Frame metadata (sequence, timestamp, dimensions, format).
        [[nodiscard]] const frame_metadata& metadata() const noexcept { return metadata_; }

    private:
        friend class capture;

        /// @brief A driver buffer that has just been dequeued, with everything a view needs to expose and re-enqueue it.
        struct dequeued_buffer
        {
            /// @brief The capture device descriptor used to re-enqueue the buffer.
            fd_t device_fd {};
            /// @brief The driver buffer index the view owns.
            std::uint32_t index {};
            /// @brief The mapped start of the buffer.
            const std::byte* ptr {};
            /// @brief The number of valid bytes in the buffer.
            std::size_t length {};
            /// @brief The frame metadata reported by the driver.
            frame_metadata metadata {};
            /// @brief Weak reference to the owning capture, so a destroyed device is not touched.
            std::weak_ptr<void> device_lifetime {};
        };

        /// @brief Constructs a view over one mmap'd driver buffer.
        /// @param buffer The dequeued buffer the view takes over.
        explicit frame_view(dequeued_buffer buffer) noexcept;

        /// @brief The capture device descriptor used to re-enqueue the buffer.
        fd_t device_fd_ {};
        /// @brief The driver buffer index this view owns.
        std::uint32_t index_ {};
        /// @brief Start of the mapped buffer.
        const std::byte* ptr_ {};
        /// @brief Number of valid bytes in the buffer.
        std::size_t length_ {};
        /// @brief Frame metadata reported by the driver.
        frame_metadata metadata_ {};
        /// @brief Weak reference to the owning capture; expired once the device is gone.
        std::weak_ptr<void> device_lifetime_;
        /// @brief Cleared by a move, so only the surviving view re-enqueues the buffer.
        bool active_ {true};
    };

}
#endif // KMX_AIO_FEATURE_READINESS
