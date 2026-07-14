/// @file aio/completion/v4l2/capture.hpp
/// @brief Completion-model V4L2 video capture using io_uring poll for async frame notification.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <expected>
    #include <memory>
    #include <span>
    #include <system_error>
    #include <vector>

    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/completion/io_base.hpp>
    #include <kmx/aio/error_code.hpp>
    #include <kmx/aio/readiness/v4l2/v4l2_types.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::completion::v4l2
{
    // Bring shared V4L2 domain types into the completion::v4l2 namespace so client code
    // using only this header can reference them without the readiness:: prefix.
    using kmx::aio::readiness::v4l2::capture_config;
    using kmx::aio::readiness::v4l2::frame_metadata;
    using kmx::aio::readiness::v4l2::frame_rate;
    using kmx::aio::readiness::v4l2::frame_size;
    using kmx::aio::readiness::v4l2::pixel_format;
    namespace fourcc = kmx::aio::readiness::v4l2::fourcc;

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
        [[nodiscard]] std::span<const std::byte> data() const noexcept;

        /// @brief Frame metadata (sequence, timestamp, dimensions, format).
        [[nodiscard]] const frame_metadata& metadata() const noexcept { return metadata_; }

    private:
        friend class capture;

        /// @brief Creates a frame view for the provided device buffer.
        frame_view(fd_t device_fd, std::uint32_t index, const std::byte* ptr, std::size_t length, frame_metadata metadata,
                   std::weak_ptr<void> device_lifetime) noexcept;

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

    /// @brief Async V4L2 video capture device — completion (io_uring) model.
    ///
    /// Opens a V4L2 capture device, allocates MMAP streaming buffers, and exposes a
    /// coroutine `next_frame()` that suspends via `IORING_OP_POLL_ADD` until the driver
    /// has a filled buffer ready. The same `completion::executor` can simultaneously drive
    /// sockets, timers, and camera capture in a single io_uring ring.
    ///
    /// ## Execution model
    /// Unlike the readiness model (`epoll`), each call to `next_frame()` submits a
    /// one-shot poll SQE to the completion executor's io_uring ring. When the kernel
    /// signals the V4L2 fd as readable (`POLLIN`), the coroutine is resumed and
    /// `VIDIOC_DQBUF` is called synchronously (the fd is opened `O_NONBLOCK` so it
    /// never blocks). The buffer is returned as a `frame_view` and is re-enqueued
    /// (VIDIOC_QBUF) when the `frame_view` is destroyed.
    ///
    /// ## Kernel constraint
    /// V4L2 MMAP buffers are device-backed (`mmap(MAP_SHARED, v4l2_fd, ...)`) and
    /// cannot be registered with `io_uring_register_buffers`, which requires anonymous
    /// non-file-backed memory. Zero-copy camera-to-network requires a separate DMABUF
    /// or USERPTR pathway.
    ///
    /// ## Typical usage
    /// @code
    ///   completion::executor exec;
    ///   auto cap = completion::v4l2::capture::create(exec, {
    ///       .device = "/dev/video0",
    ///       .format = completion::v4l2::fourcc::yuyv,
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
    class capture: public io_base
    {
    public:
        using frame_result = task<std::expected<frame_view, kmx::aio::error_code>>;
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
        ///   8. `VIDIOC_STREAMON` — start streaming
        ///
        /// @param exec  Completion executor to drive io_uring poll events.
        /// @param cfg   Device configuration (device path, format, size, buffer count).
        /// @return A fully initialised `capture` ready for `next_frame()`, or an error.
        [[nodiscard]] static create_result create(executor& exec, capture_config cfg) noexcept;

        capture(capture&&) noexcept;
        capture& operator=(capture&&) noexcept = delete;
        ~capture() noexcept;

        /// @brief Suspends via io_uring poll until the driver has a filled frame, then returns it.
        ///
        /// Issues a one-shot `IORING_OP_POLL_ADD` SQE for the V4L2 fd. When the kernel
        /// signals `POLLIN`, calls `VIDIOC_DQBUF` and returns the frame. If `VIDIOC_DQBUF`
        /// returns `EAGAIN` (spurious wakeup), re-arms the poll and retries.
        [[nodiscard]] frame_result next_frame() noexcept(false);

        /// @brief Returns the negotiated configuration (may differ from requested).
        [[nodiscard]] const capture_config& config() const noexcept { return config_; }

        /// @brief Stops streaming (VIDIOC_STREAMOFF). Idempotent.
        [[nodiscard]] std::expected<void, kmx::aio::error_code> stream_off() noexcept;

        /// @brief Restarts streaming after `stream_off()`.
        [[nodiscard]] std::expected<void, kmx::aio::error_code> stream_on() noexcept;

    private:
        /// @brief Describes one MMAP buffer mapped from the V4L2 driver.
        struct mmap_buffer
        {
            /// @brief Mapped buffer address.
            void* ptr {};
            /// @brief Size of the mapped buffer in bytes.
            std::size_t length {};
        };

        /// @brief Creates an initialized capture object.
        capture(executor& exec, file_descriptor&& fd, capture_config cfg, std::vector<mmap_buffer> buffers) noexcept;

        /// @brief Opens the V4L2 device described by the configuration.
        [[nodiscard]] static std::expected<file_descriptor, kmx::aio::error_code> open_device(const capture_config& cfg) noexcept;
        /// @brief Verifies the device supports the required capture capabilities.
        [[nodiscard]] static std::expected<void, kmx::aio::error_code> validate_capabilities(fd_t device_fd) noexcept;
        /// @brief Negotiates the device format and dimensions.
        [[nodiscard]] static std::expected<void, kmx::aio::error_code> negotiate_format(fd_t device_fd, capture_config& cfg) noexcept;
        /// @brief Negotiates the device frame rate when supported.
        [[nodiscard]] static std::expected<void, kmx::aio::error_code> negotiate_frame_rate(fd_t device_fd,
                                                                                            const capture_config& cfg) noexcept;
        /// @brief Allocates and maps the MMAP buffer set.
        [[nodiscard]] static std::expected<std::vector<mmap_buffer>, kmx::aio::error_code> request_and_map_buffers(
            fd_t device_fd, capture_config& cfg) noexcept;
        /// @brief Queues every mapped buffer so streaming can start immediately.
        [[nodiscard]] static std::expected<void, kmx::aio::error_code> queue_all_buffers(fd_t device_fd,
                                                                                         const std::vector<mmap_buffer>& buffers) noexcept;
        /// @brief Starts the V4L2 streaming engine.
        [[nodiscard]] static std::expected<void, kmx::aio::error_code> start_streaming(fd_t device_fd) noexcept;
        /// @brief Unmaps the provided buffer set.
        static void unmap_buffers(std::vector<mmap_buffer>& buffers) noexcept;

        /// @brief Unmaps all mmap'd buffers. Called from destructor and failed create().
        void unmap_buffers() noexcept;

        /// @brief Negotiated capture configuration.
        capture_config config_;
        /// @brief All MMAP buffers owned by the capture object.
        std::vector<mmap_buffer> buffers_;
        /// @brief Lifetime token shared with outstanding frame views.
        std::shared_ptr<void> device_lifetime_ {std::make_shared<int>(0)};
        /// @brief Indicates whether streaming is currently active.
        bool streaming_ {};
    };

} // namespace kmx::aio::completion::v4l2
