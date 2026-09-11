/// @file api/kmx/aio/readiness/v4l2/capture.hpp
/// @brief Readiness-model V4L2 video capture using epoll for async frame notification.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_READINESS)
    #ifndef PCH
        #include <kmx/aio/error_code.hpp>
        #include <kmx/aio/file_descriptor.hpp>
        #include <kmx/aio/readiness/executor.hpp>
        #include <kmx/aio/readiness/io_base.hpp>
        #include <kmx/aio/readiness/v4l2/frame_view.hpp>
        #include <kmx/aio/readiness/v4l2/v4l2_types.hpp>
        #include <kmx/aio/task.hpp>

        #include <cstddef>
        #include <cstdint>
        #include <expected>
        #include <memory>
        #include <vector>
    #endif

namespace kmx::aio::readiness::v4l2
{
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
        ~capture() noexcept;

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

        /// @brief An opened capture device, or why it could not be opened.
        using expected_fd = std::expected<file_descriptor, kmx::aio::error_code>;
        /// @brief A complete set of mapped driver buffers, or why they could not be mapped.
        using expected_mmap_buffers = std::expected<std::vector<mmap_buffer>, kmx::aio::error_code>;

        /// @brief Opens the device node and checks it can capture video by streaming.
        [[nodiscard]] static expected_fd open_capture_device(const capture_config& cfg) noexcept;
        /// @brief Negotiates the pixel format, frame size and rate, rewriting @p cfg to what was granted.
        [[nodiscard]] static std::expected<void, kmx::aio::error_code> negotiate_format(int fd, capture_config& cfg) noexcept;
        /// @brief Requests the driver's buffers and maps every one of them into this process.
        [[nodiscard]] static expected_mmap_buffers map_buffers(int fd, capture_config& cfg) noexcept;
        /// @brief Hands every buffer to the driver so streaming has somewhere to deliver into.
        [[nodiscard]] static std::expected<void, kmx::aio::error_code> enqueue_buffers(int fd, std::uint32_t count) noexcept;
        /// @brief Releases every mapping in @p buffers.
        static void unmap_all(std::vector<mmap_buffer>& buffers) noexcept;

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

}
#endif // KMX_AIO_FEATURE_READINESS
