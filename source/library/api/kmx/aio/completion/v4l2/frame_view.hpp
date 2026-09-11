/// @file api/kmx/aio/completion/v4l2/frame_view.hpp
/// @brief Zero-copy view of one captured V4L2 frame that re-enqueues its buffer on destruction — completion (io_uring) model.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_COMPLETION)
    #ifndef PCH
        #include <kmx/aio/basic_types.hpp>
        #include <kmx/aio/readiness/v4l2/v4l2_types.hpp>

        #include <cstddef>
        #include <cstdint>
        #include <memory>
    #endif

namespace kmx::aio::completion::v4l2
{
    // Bring the shared V4L2 frame metadata type into the completion::v4l2 namespace so client code
    // using only this header can reference it without the readiness:: prefix.
    using kmx::aio::readiness::v4l2::frame_metadata;

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
        /// @brief Creates a disabled frame view.
        frame_view() = delete;
        /// @brief Disables copying to keep buffer ownership unique.
        frame_view(const frame_view&) = delete;
        /// @brief Disables copying to keep buffer ownership unique.
        frame_view& operator=(const frame_view&) = delete;

        /// @brief Move constructor — transfers ownership of the buffer slot.
        frame_view(frame_view&&) noexcept;

        /// @brief Move assignment is disabled to keep ownership unambiguous.
        frame_view& operator=(frame_view&&) noexcept = delete;

        /// @brief Returns the buffer to the driver (VIDIOC_QBUF).
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
            /// @brief Device file descriptor used to requeue the buffer.
            fd_t device_fd {};
            /// @brief Kernel buffer index associated with the frame.
            std::uint32_t index {};
            /// @brief Pointer to the mapped frame bytes.
            const std::byte* ptr {};
            /// @brief Length of the mapped frame bytes.
            std::size_t length {};
            /// @brief Metadata captured with the frame.
            frame_metadata metadata {};
            /// @brief Lifetime token for the owning capture device.
            std::weak_ptr<void> device_lifetime {};
        };

        /// @brief Creates a frame view for the provided device buffer.
        /// @param buffer The dequeued buffer the view takes over.
        explicit frame_view(dequeued_buffer buffer) noexcept;

        /// @brief Device file descriptor used to requeue the buffer.
        fd_t device_fd_ {};
        /// @brief Kernel buffer index associated with this frame.
        std::uint32_t index_ {};
        /// @brief Pointer to the mapped frame bytes.
        const std::byte* ptr_ {};
        /// @brief Length of the mapped frame bytes.
        std::size_t length_ {};
        /// @brief Metadata captured with the frame.
        frame_metadata metadata_ {};
        /// @brief Lifetime token for the owning capture device.
        std::weak_ptr<void> device_lifetime_;
        /// @brief Indicates whether the frame is still responsible for requeueing.
        bool active_ {true};
    };

}
#endif // KMX_AIO_FEATURE_COMPLETION
