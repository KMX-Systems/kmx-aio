/// @file aio/readiness/v4l2/capture.hpp
/// @brief Readiness-model V4L2 video capture using epoll for async frame notification.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <memory>
    #include <span>
    #include <vector>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/error_code.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/io_base.hpp>
    #include <kmx/aio/readiness/v4l2/v4l2_types.hpp>
    #include <kmx/aio/task.hpp>
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

        /// @brief Constructs a view over one mmap'd driver buffer.
        /// @param device_fd        The capture device descriptor used to re-enqueue the buffer.
        /// @param index            The driver buffer index this view owns.
        /// @param ptr              The mapped start of the buffer.
        /// @param length           The number of valid bytes in the buffer.
        /// @param metadata         The frame metadata reported by the driver.
        /// @param device_lifetime  Weak reference to the owning capture, so a destroyed device is not touched.
        frame_view(fd_t device_fd, std::uint32_t index, const std::byte* ptr, std::size_t length, frame_metadata metadata,
                   std::weak_ptr<void> device_lifetime) noexcept;

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

    /// @brief Async V4L2 video capture device.
    ///
    /// Opens a V4L2 capture device, allocates MMAP streaming buffers, and exposes a
    /// coroutine `next_frame()` that suspends via epoll until the driver has a filled
    /// buffer ready.  The device starts streaming immediately after successful `create()`.
    ///
    /// ## Typical usage
    /// @code
    ///   auto cap = readiness::v4l2::capture::create(exec, {
    ///       .device = "/dev/video0",
    ///       .format = readiness::v4l2::fourcc::nv12,
    ///       .size   = {1920u, 1080u},
    ///   });
    ///   if (!cap) { /* handle error */ }
    ///
    ///   while (true) {
    ///       auto frame = co_await cap->next_frame();
    ///       if (!frame) break;
    ///       process(frame->data());
    ///       // buffer is automatically re-enqueued when `frame` goes out of scope
    ///   }
    /// @endcode
    ///
    /// @note Requires a V4L2 device capable of MMAP streaming (V4L2_CAP_STREAMING).
    ///       Most USB webcams and ISP/CSI-2 pipelines (GMSL, MIPI) satisfy this.
    class capture: public io_base
    {
    public:
        /// @brief A task yielding the next @ref frame_view, or the error that ended the capture.
        using frame_result = task<std::expected<frame_view, kmx::aio::error_code>>;
        /// @brief A configured @ref capture, or the error code explaining why one could not be created.
        using create_result = std::expected<capture, kmx::aio::error_code>;

        /// @brief Opens and configures a V4L2 capture device.
        ///
        /// Steps performed:
        ///   1. `open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC)`
        ///   2. `VIDIOC_QUERYCAP`  — verify capture + streaming capability
        ///   3. `VIDIOC_S_FMT`    — negotiate pixel format, width, height
        ///   4. `VIDIOC_S_PARM`   — negotiate frame rate (best-effort)
        ///   5. `VIDIOC_REQBUFS`  — allocate `cfg.buffer_count` MMAP buffers
        ///   6. `VIDIOC_QUERYBUF` + `mmap()` per buffer
        ///   7. `VIDIOC_QBUF` for each buffer to prime the driver queue
        ///   8. `exec.register_fd()` — register fd with epoll
        ///   9. `VIDIOC_STREAMON` — start streaming
        ///
        /// @param exec  Readiness executor to drive epoll events.
        /// @param cfg   Device configuration (device path, format, size, buffer count).
        /// @return A fully initialised `capture` ready for `next_frame()`, or an error.
        [[nodiscard]] static create_result create(executor& exec, capture_config cfg) noexcept;

        /// @brief Move constructor — transfers ownership of the device and its mapped buffers.
        capture(capture&&) noexcept;
        /// @brief Move assignment is disabled to keep ownership unambiguous.
        capture& operator=(capture&&) noexcept = delete;
        /// @brief Stops streaming, unmaps every buffer, and closes the device.
        ~capture() noexcept override;

        /// @brief Suspends until the driver has a filled frame, then returns it.
        ///
        /// The returned `frame_view` holds the kernel buffer until it is destroyed,
        /// at which point the buffer is re-enqueued (VIDIOC_QBUF) automatically.
        /// Only one outstanding `frame_view` per buffer index is safe; the natural
        /// coroutine control flow enforces this when each co_await result is scoped
        /// to its enclosing block.
        [[nodiscard]] frame_result next_frame() noexcept(false);

        /// @brief Returns the negotiated configuration (may differ from requested).
        [[nodiscard]] const capture_config& config() const noexcept { return config_; }

        /// @brief Stops streaming (VIDIOC_STREAMOFF). Idempotent.
        [[nodiscard]] std::expected<void, kmx::aio::error_code> stream_off() noexcept;

        /// @brief Restarts streaming after `stream_off()`.
        [[nodiscard]] std::expected<void, kmx::aio::error_code> stream_on() noexcept;

    private:
        /// @brief One MMAP'd driver buffer: its mapped address and length.
        struct mmap_buffer
        {
            /// @brief Start of the mapping, or null when the buffer was never mapped.
            void* ptr {};
            /// @brief Length of the mapping in bytes.
            std::size_t length {};
        };

        /// @brief Constructs a streaming capture from resources @ref create has already acquired.
        /// @param exec    The executor the device descriptor is registered with.
        /// @param fd      The opened capture device.
        /// @param cfg     The negotiated configuration.
        /// @param buffers The mapped driver buffers.
        capture(executor& exec, file_descriptor&& fd, capture_config cfg, std::vector<mmap_buffer> buffers) noexcept;

        /// @brief Unmaps all mmap'd buffers. Called from destructor and failed create().
        void unmap_buffers() noexcept;

        /// @brief The negotiated configuration, as accepted by the driver.
        capture_config config_;
        /// @brief The mapped driver buffers, indexed by driver buffer index.
        std::vector<mmap_buffer> buffers_;
        /// @brief Lifetime token weakly held by every @ref frame_view this device hands out.
        std::shared_ptr<void> device_lifetime_ {std::make_shared<int>(0)};
        /// @brief `true` between @ref stream_on and @ref stream_off.
        bool streaming_ {};
    };

} // namespace kmx::aio::readiness::v4l2
