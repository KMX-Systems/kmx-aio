/// @file aio/readiness/v4l2/capture.cpp
/// @brief Async V4L2 video capture — readiness (epoll) model implementation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/readiness/v4l2/capture.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/videodev2.h>

#include <kmx/aio/basic_types.hpp>
#include <kmx/aio/error_code.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::readiness::v4l2
{
    // frame_view

    frame_view::frame_view(const fd_t device_fd, const std::uint32_t index, const std::byte* const ptr,
                           const std::size_t length, frame_metadata metadata,
                           std::weak_ptr<void> device_lifetime) noexcept:
        device_fd_(device_fd),
        index_(index),
        ptr_(ptr),
        length_(length),
        metadata_(metadata),
        device_lifetime_(std::move(device_lifetime))
    {
    }

    frame_view::frame_view(frame_view&& other) noexcept:
        device_fd_(other.device_fd_),
        index_(other.index_),
        ptr_(other.ptr_),
        length_(other.length_),
        metadata_(other.metadata_),
        device_lifetime_(std::move(other.device_lifetime_)),
        active_(std::exchange(other.active_, false))
    {
    }

    frame_view::~frame_view() noexcept
    {
        if (!active_ || device_lifetime_.expired())
            return;

        ::v4l2_buffer buf {};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = index_;

        if (::ioctl(device_fd_, VIDIOC_QBUF, &buf) < 0)
        {
            // Best-effort: log but do not throw from a destructor.
            kmx::logger::log(kmx::logger::level::warn, std::source_location::current(),
                             "VIDIOC_QBUF failed for buffer {}: {}", index_, std::strerror(errno));
        }
    }

    std::span<const std::byte> frame_view::data() const noexcept
    {
        return { ptr_, metadata_.bytes_used };
    }

    // capture — private constructor

    capture::capture(executor& exec, file_descriptor&& fd, capture_config cfg,
                     std::vector<mmap_buffer> buffers) noexcept:
        io_base(exec, std::move(fd)),
        config_(std::move(cfg)),
        buffers_(std::move(buffers))
    {
    }

    capture::capture(capture&& other) noexcept:
        io_base(std::move(other)),
        config_(std::move(other.config_)),
        buffers_(std::move(other.buffers_)),
        device_lifetime_(std::move(other.device_lifetime_)),
        streaming_(std::exchange(other.streaming_, false))
    {
    }

    // capture::create()

    capture::create_result capture::create(executor& exec, capture_config cfg) noexcept
    {
        // 1. Open the device node.
        const int raw_fd = ::open(cfg.device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC); // NOLINT(cppcoreguidelines-pro-type-vararg)
        if (raw_fd < 0)
            return std::unexpected(kmx::aio::from_errno(errno));

        file_descriptor fd { raw_fd };

        // 2. Query capabilities.
        ::v4l2_capability cap {};
        if (::ioctl(raw_fd, VIDIOC_QUERYCAP, &cap) < 0)
            return std::unexpected(kmx::aio::from_errno(errno));

        const auto caps = ((cap.capabilities & V4L2_CAP_DEVICE_CAPS) != 0u) ? cap.device_caps : cap.capabilities;

        if ((caps & V4L2_CAP_VIDEO_CAPTURE) == 0u)
            return std::unexpected(kmx::aio::error_code::unsupported_operation);

        if ((caps & V4L2_CAP_STREAMING) == 0u)
            return std::unexpected(kmx::aio::error_code::unsupported_operation);

        // 3. Negotiate pixel format and frame size.
        ::v4l2_format fmt {};
        fmt.type                  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width         = cfg.size.width;
        fmt.fmt.pix.height        = cfg.size.height;
        fmt.fmt.pix.pixelformat   = cfg.format.fourcc;
        fmt.fmt.pix.field         = V4L2_FIELD_NONE;

        if (::ioctl(raw_fd, VIDIOC_S_FMT, &fmt) < 0)
            return std::unexpected(kmx::aio::from_errno(errno));

        // Reflect what the driver actually negotiated.
        cfg.size.width    = fmt.fmt.pix.width;
        cfg.size.height   = fmt.fmt.pix.height;
        cfg.format.fourcc = fmt.fmt.pix.pixelformat;

        // 4. Negotiate frame rate (best-effort; not all drivers support VIDIOC_S_PARM).
        {
            ::v4l2_streamparm parm {};
            parm.type                                  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            parm.parm.capture.timeperframe.numerator   = cfg.fps.numerator;
            parm.parm.capture.timeperframe.denominator = cfg.fps.denominator;
            // Ignore failure — frame rate negotiation is advisory.
            (void)::ioctl(raw_fd, VIDIOC_S_PARM, &parm);
        }

        // 5. Request MMAP buffers from the driver.
        ::v4l2_requestbuffers req {};
        req.count  = cfg.buffer_count;
        req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (::ioctl(raw_fd, VIDIOC_REQBUFS, &req) < 0)
            return std::unexpected(kmx::aio::from_errno(errno));

        if (req.count == 0)
            return std::unexpected(kmx::aio::error_code::internal_error);

        cfg.buffer_count = req.count; // Driver may grant fewer buffers than requested.

        // 6. Map each buffer into userspace.
        std::vector<mmap_buffer> buffers;
        buffers.reserve(req.count);

        for (std::uint32_t i = 0u; i < req.count; ++i)
        {
            ::v4l2_buffer buf {};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = i;

            if (::ioctl(raw_fd, VIDIOC_QUERYBUF, &buf) < 0)
            {
                // Unmap already mapped buffers before returning.
                for (auto& b : buffers)
                {
                    if (b.ptr && b.ptr != MAP_FAILED)
                        ::munmap(b.ptr, b.length);
                }
                return std::unexpected(kmx::aio::from_errno(errno));
            }

            void* const ptr = ::mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, raw_fd, static_cast<off_t>(buf.m.offset));

            if (ptr == MAP_FAILED) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
            {
                for (auto& b : buffers)
                {
                    if (b.ptr && b.ptr != MAP_FAILED)
                        ::munmap(b.ptr, b.length);
                }
                return std::unexpected(kmx::aio::from_errno(errno));
            }

            buffers.push_back({ ptr, buf.length });
        }

        // 7. Enqueue all buffers to prime the driver queue.
        for (std::uint32_t i = 0u; i < req.count; ++i)
        {
            ::v4l2_buffer buf {};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = i;

            if (::ioctl(raw_fd, VIDIOC_QBUF, &buf) < 0)
            {
                for (auto& b : buffers)
                    ::munmap(b.ptr, b.length);
                return std::unexpected(kmx::aio::from_errno(errno));
            }
        }

        // 8. Register fd with epoll.
        if (const auto reg = exec.register_fd(raw_fd); !reg)
        {
            for (auto& b : buffers)
                ::munmap(b.ptr, b.length);
            return std::unexpected(kmx::aio::from_errno(reg.error().value()));
        }

        // 9. Start streaming.
        const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (::ioctl(raw_fd, VIDIOC_STREAMON, &type) < 0)
        {
            exec.unregister_fd(raw_fd);
            for (auto& b : buffers)
                ::munmap(b.ptr, b.length);
            return std::unexpected(kmx::aio::from_errno(errno));
        }

        capture result { exec, std::move(fd), std::move(cfg), std::move(buffers) };
        result.streaming_ = true;
        return result;
    }

    // capture::~capture()

    void capture::unmap_buffers() noexcept
    {
        for (auto& buf : buffers_)
        {
            if (buf.ptr && buf.ptr != MAP_FAILED)
                ::munmap(buf.ptr, buf.length);
        }
        buffers_.clear();
    }

    capture::~capture() noexcept
    {
        if (fd_.is_valid())
        {
            if (streaming_)
            {
                const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                (void)::ioctl(fd_.get(), VIDIOC_STREAMOFF, &type);
            }
        }
        unmap_buffers();
        // device_lifetime_ shared_ptr destruction signals all outstanding frame_views.
        // io_base destructor handles unregister_fd + fd close.
    }

    // capture::stream_on / stream_off

    std::expected<void, kmx::aio::error_code> capture::stream_on() noexcept
    {
        if (streaming_)
            return {};

        const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (::ioctl(fd_.get(), VIDIOC_STREAMON, &type) < 0)
            return std::unexpected(kmx::aio::from_errno(errno));

        streaming_ = true;
        return {};
    }

    std::expected<void, kmx::aio::error_code> capture::stream_off() noexcept
    {
        if (!streaming_)
            return {};

        const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (::ioctl(fd_.get(), VIDIOC_STREAMOFF, &type) < 0)
            return std::unexpected(kmx::aio::from_errno(errno));

        streaming_ = false;
        return {};
    }

    // capture::next_frame()

    capture::frame_result capture::next_frame() noexcept(false)
    {
        while (true)
        {
            // Suspend until the driver signals the fd readable (a buffer is filled).
            co_await exec_.wait_io(fd_.get(), event_type::read);

            ::v4l2_buffer buf {};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            if (::ioctl(fd_.get(), VIDIOC_DQBUF, &buf) < 0)
            {
                if (would_block(errno))
                    continue;
                co_return std::unexpected(kmx::aio::from_errno(errno));
            }

            const auto& mapped = buffers_[buf.index];

            const std::uint64_t timestamp_ns =
                static_cast<std::uint64_t>(buf.timestamp.tv_sec) * 1'000'000'000ull +
                static_cast<std::uint64_t>(buf.timestamp.tv_usec) * 1'000ull;

            frame_metadata meta {
                .sequence     = buf.sequence,
                .timestamp_ns = timestamp_ns,
                .bytes_used   = buf.bytesused,
                .width        = config_.size.width,
                .height       = config_.size.height,
                .fourcc       = config_.format.fourcc,
            };

            co_return frame_view {
                fd_.get(),
                buf.index,
                static_cast<const std::byte*>(mapped.ptr),
                mapped.length,
                meta,
                device_lifetime_,
            };
        }
    }

} // namespace kmx::aio::readiness::v4l2
