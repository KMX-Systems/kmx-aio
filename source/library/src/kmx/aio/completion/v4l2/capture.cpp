/// @file aio/completion/v4l2/capture.cpp
/// @brief Async V4L2 video capture — completion (io_uring) model implementation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include "kmx/aio/completion/v4l2/capture.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/videodev2.h>
#include <poll.h>

#include <kmx/aio/basic_types.hpp>
#include <kmx/aio/error_code.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::completion::v4l2
{
    // frame_view

    frame_view::frame_view(const fd_t device_fd, const std::uint32_t index, const std::byte* const ptr, const std::size_t length,
                           frame_metadata metadata, std::weak_ptr<void> device_lifetime) noexcept:
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
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = index_;

        if (::ioctl(device_fd_, VIDIOC_QBUF, &buf) < 0)
            kmx::logger::log(kmx::logger::level::warn, std::source_location::current(), "VIDIOC_QBUF failed for buffer {}: {}", index_,
                             std::strerror(errno));
    }

    std::span<const std::byte> frame_view::data() const noexcept
    {
        return {ptr_, metadata_.bytes_used};
    }

    // capture — private constructor

    capture::capture(executor& exec, file_descriptor&& fd, capture_config cfg, std::vector<mmap_buffer> buffers) noexcept:
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

    std::expected<file_descriptor, kmx::aio::error_code> capture::open_device(const capture_config& cfg) noexcept
    {
        const int raw_fd = ::open(cfg.device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC); // NOLINT(cppcoreguidelines-pro-type-vararg)
        if (raw_fd < 0)
            return std::unexpected(kmx::aio::from_errno(errno));

        return file_descriptor {raw_fd};
    }

    std::expected<void, kmx::aio::error_code> capture::validate_capabilities(const fd_t device_fd) noexcept
    {
        ::v4l2_capability cap {};
        if (::ioctl(device_fd, VIDIOC_QUERYCAP, &cap) < 0)
            return std::unexpected(kmx::aio::from_errno(errno));

        const auto caps = ((cap.capabilities & V4L2_CAP_DEVICE_CAPS) != 0u) ? cap.device_caps : cap.capabilities;
        if ((caps & V4L2_CAP_VIDEO_CAPTURE) == 0u)
            return std::unexpected(kmx::aio::error_code::unsupported_operation);

        if ((caps & V4L2_CAP_STREAMING) == 0u)
            return std::unexpected(kmx::aio::error_code::unsupported_operation);

        return {};
    }

    std::expected<void, kmx::aio::error_code> capture::negotiate_format(const fd_t device_fd, capture_config& cfg) noexcept
    {
        ::v4l2_format fmt {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        auto& pix = fmt.fmt.pix;
        pix.width = cfg.size.width;
        pix.height = cfg.size.height;
        pix.pixelformat = cfg.format.fourcc;
        pix.field = V4L2_FIELD_NONE;

        if (::ioctl(device_fd, VIDIOC_S_FMT, &fmt) < 0)
            return std::unexpected(kmx::aio::from_errno(errno));

        const auto& src_pix = fmt.fmt.pix;
        cfg.size.width = src_pix.width;
        cfg.size.height = src_pix.height;
        cfg.format.fourcc = src_pix.pixelformat;
        return {};
    }

    std::expected<void, kmx::aio::error_code> capture::negotiate_frame_rate(const fd_t device_fd, const capture_config& cfg) noexcept
    {
        ::v4l2_streamparm parm {};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        auto& timeperframe = parm.parm.capture.timeperframe;
        timeperframe.numerator = cfg.fps.numerator;
        timeperframe.denominator = cfg.fps.denominator;
        (void) ::ioctl(device_fd, VIDIOC_S_PARM, &parm);
        return {};
    }

    std::expected<std::vector<capture::mmap_buffer>, kmx::aio::error_code> capture::request_and_map_buffers(const fd_t device_fd,
                                                                                                            capture_config& cfg) noexcept
    {
        ::v4l2_requestbuffers req {};
        req.count = cfg.buffer_count;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (::ioctl(device_fd, VIDIOC_REQBUFS, &req) < 0)
            return std::unexpected(kmx::aio::from_errno(errno));

        if (req.count == 0)
            return std::unexpected(kmx::aio::error_code::internal_error);

        cfg.buffer_count = req.count;

        std::vector<mmap_buffer> buffers;
        buffers.reserve(req.count);

        for (std::uint32_t i = 0u; i < req.count; ++i)
        {
            ::v4l2_buffer buf {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            if (::ioctl(device_fd, VIDIOC_QUERYBUF, &buf) < 0)
            {
                unmap_buffers(buffers);
                return std::unexpected(kmx::aio::from_errno(errno));
            }

            void* const ptr = ::mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, static_cast<off_t>(buf.m.offset));
            if (ptr == MAP_FAILED) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
            {
                unmap_buffers(buffers);
                return std::unexpected(kmx::aio::from_errno(errno));
            }

            buffers.push_back({ptr, buf.length});
        }

        return buffers;
    }

    std::expected<void, kmx::aio::error_code> capture::queue_all_buffers(const fd_t device_fd,
                                                                         const std::vector<mmap_buffer>& buffers) noexcept
    {
        for (std::uint32_t i = 0u; i < buffers.size(); ++i)
        {
            ::v4l2_buffer buf {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            if (::ioctl(device_fd, VIDIOC_QBUF, &buf) < 0)
                return std::unexpected(kmx::aio::from_errno(errno));
        }

        return {};
    }

    std::expected<void, kmx::aio::error_code> capture::start_streaming(const fd_t device_fd) noexcept
    {
        const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (::ioctl(device_fd, VIDIOC_STREAMON, &type) < 0)
            return std::unexpected(kmx::aio::from_errno(errno));

        return {};
    }

    // capture::create()

    capture::create_result capture::create(executor& exec, capture_config cfg) noexcept
    {
        auto fd = open_device(cfg);
        if (!fd)
            return std::unexpected(fd.error());

        if (const auto caps = validate_capabilities(fd->get()); !caps)
            return std::unexpected(caps.error());

        if (const auto fmt = negotiate_format(fd->get(), cfg); !fmt)
            return std::unexpected(fmt.error());

        if (const auto fps = negotiate_frame_rate(fd->get(), cfg); !fps)
            return std::unexpected(fps.error());

        auto buffers = request_and_map_buffers(fd->get(), cfg);
        if (!buffers)
            return std::unexpected(buffers.error());

        if (const auto queued = queue_all_buffers(fd->get(), *buffers); !queued)
        {
            unmap_buffers(*buffers);
            return std::unexpected(queued.error());
        }

        if (const auto stream = start_streaming(fd->get()); !stream)
        {
            unmap_buffers(*buffers);
            return std::unexpected(stream.error());
        }

        capture result {exec, std::move(*fd), std::move(cfg), std::move(*buffers)};
        result.streaming_ = true;
        return result;
    }

    // capture::~capture()

    void capture::unmap_buffers() noexcept
    {
        unmap_buffers(buffers_);
    }

    void capture::unmap_buffers(std::vector<mmap_buffer>& buffers) noexcept
    {
        for (auto& buf: buffers)
            if (buf.ptr && buf.ptr != MAP_FAILED)
                ::munmap(buf.ptr, buf.length);

        buffers.clear();
    }

    capture::~capture() noexcept
    {
        if ((fd_.get() >= 0) && streaming_)
        {
            const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            static_cast<void>(::ioctl(fd_.get(), VIDIOC_STREAMOFF, &type));
        }

        unmap_buffers();
        // device_lifetime_ shared_ptr destruction signals all outstanding frame_views.
        // io_base destructor handles fd close.
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
            // Suspend the coroutine until the V4L2 fd signals readiness via io_uring poll.
            // IORING_OP_POLL_ADD shares the same completion ring as sockets and timers,
            // so camera capture and networking coexist in one executor without epoll.
            const auto poll_res = co_await exec_.async_poll(fd_.get(), POLLIN | POLLERR | POLLHUP | POLLPRI);
            if (!poll_res)
                co_return std::unexpected(kmx::aio::from_errno(poll_res.error().value()));

            ::v4l2_buffer buf {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            if (::ioctl(fd_.get(), VIDIOC_DQBUF, &buf) < 0)
            {
                if (would_block(errno))
                    continue; // Spurious wakeup — re-arm poll and retry.
                co_return std::unexpected(kmx::aio::from_errno(errno));
            }

            if (buf.index >= static_cast<std::uint32_t>(buffers_.size()))
                co_return std::unexpected(kmx::aio::error_code::internal_error);

            const auto& mapped = buffers_[buf.index];

            if (buf.bytesused > mapped.length)
                co_return std::unexpected(kmx::aio::error_code::internal_error);

            const std::uint64_t timestamp_ns = static_cast<std::uint64_t>(buf.timestamp.tv_sec) * 1'000'000'000ull +
                                               static_cast<std::uint64_t>(buf.timestamp.tv_usec) * 1'000ull;

            frame_metadata meta {
                .sequence = buf.sequence,
                .timestamp_ns = timestamp_ns,
                .bytes_used = buf.bytesused,
                .width = config_.size.width,
                .height = config_.size.height,
                .fourcc = config_.format.fourcc,
            };

            co_return frame_view {
                fd_.get(), buf.index, static_cast<const std::byte*>(mapped.ptr), mapped.length, meta, device_lifetime_,
            };
        }
    }

} // namespace kmx::aio::completion::v4l2
