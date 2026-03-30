/// @file aio/readiness/v4l2/capture.hpp
/// @brief Readiness-model V4L2 video capture using epoll for async frame notification.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <memory>
    #include <span>
    #include <system_error>
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
        frame_view() = delete;
        frame_view(const frame_view&) = delete;
        frame_view& operator=(const frame_view&) = delete;

        /// @brief Move constructor — transfers ownership of the buffer slot.
        frame_view(frame_view&&) noexcept;

        /// @brief Move assignment is disabled to keep ownership unambiguous.
        frame_view& operator=(frame_view&&) noexcept = delete;

        /// @brief Returns the buffer to the driver.
        ~frame_view() noexcept;

        /// @brief Raw frame bytes (zero-copy view into the mmap'd kernel buffer).
        [[nodiscard]] std::span<const std::byte> data() const noexcept;

        /// @brief Frame metadata (sequence, timestamp, dimensions, format).
        [[nodiscard]] const frame_metadata& metadata() const noexcept { return metadata_; }

    private:
        friend class capture;

        frame_view(fd_t device_fd, std::uint32_t index, const std::byte* ptr, std::size_t length,
                   frame_metadata metadata, std::weak_ptr<void> device_lifetime) noexcept;

        fd_t                  device_fd_ {};
        std::uint32_t         index_ {};
        const std::byte*      ptr_ { nullptr };
        std::size_t           length_ {};
        frame_metadata        metadata_ {};
        std::weak_ptr<void>   device_lifetime_;
        bool                  active_ { true };
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
    class capture : public io_base
    {
    public:
        using frame_result  = task<std::expected<frame_view, kmx::aio::error_code>>;
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

        capture(capture&&) noexcept;
        capture& operator=(capture&&) noexcept = delete;
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
        struct mmap_buffer
        {
            void*       ptr    { nullptr };
            std::size_t length {};
        };

        capture(executor& exec, file_descriptor&& fd, capture_config cfg,
                std::vector<mmap_buffer> buffers) noexcept;

        /// @brief Unmaps all mmap'd buffers. Called from destructor and failed create().
        void unmap_buffers() noexcept;

        capture_config             config_;
        std::vector<mmap_buffer>   buffers_;
        std::shared_ptr<void>      device_lifetime_ { std::make_shared<int>(0) };
        bool                       streaming_ { false };
    };

} // namespace kmx::aio::readiness::v4l2
