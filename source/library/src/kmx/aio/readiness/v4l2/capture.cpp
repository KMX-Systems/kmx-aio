/// @file aio/readiness/v4l2/capture.cpp
/// @brief Async V4L2 video capture — readiness (epoll) model implementation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/readiness/v4l2/capture.hpp>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/videodev2.h>

#include <kmx/aio/basic_types.hpp>
#include <kmx/aio/error_code.hpp>
#include <kmx/logger.hpp>

namespace kmx::aio::readiness::v4l2
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
            // Best-effort: log but do not throw from a destructor.
        kmx::logger::log(kmx::logger::level::warn, std::source_location::current(), "VIDIOC_QBUF failed for buffer {}: {}", index_,
                         std::strerror(errno));
    }

    cspan_byte_t frame_view::data() const noexcept
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

    // capture::create()

    void capture::unmap_all(std::vector<mmap_buffer>& buffers) noexcept
    {
        for (auto& buffer: buffers)
            if ((buffer.ptr != nullptr) && (buffer.ptr != MAP_FAILED))
                ::munmap(buffer.ptr, buffer.length);
    }

    capture::expected_fd capture::open_capture_device(const capture_config& cfg) noexcept
    {
        const int raw_fd = ::open(cfg.device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC); // NOLINT(cppcoreguidelines-pro-type-vararg)
        if (raw_fd < 0)
            return std::unexpected(kmx::aio::from_errno(errno));

        file_descriptor fd {raw_fd};
        ::v4l2_capability cap {};
        if (::ioctl(raw_fd, VIDIOC_QUERYCAP, &cap) < 0)
            return std::unexpected(kmx::aio::from_errno(errno));

        // A driver that reports per-device capabilities describes this node with them; the older field
        // describes everything the driver offers, which may be more than this node does.
        const auto caps = ((cap.capabilities & V4L2_CAP_DEVICE_CAPS) != 0u) ? cap.device_caps : cap.capabilities;
        if (((caps & V4L2_CAP_VIDEO_CAPTURE) == 0u) || ((caps & V4L2_CAP_STREAMING) == 0u))
            return std::unexpected(kmx::aio::error_code::unsupported_operation);

        return fd;
    }

    std::expected<void, kmx::aio::error_code> capture::negotiate_format(const int fd, capture_config& cfg) noexcept
    {
        ::v4l2_format fmt {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        auto& pix = fmt.fmt.pix;
        pix.width = cfg.size.width;
        pix.height = cfg.size.height;
        pix.pixelformat = cfg.format.fourcc;
        pix.field = V4L2_FIELD_NONE;

        if (::ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
            return std::unexpected(kmx::aio::from_errno(errno));

        // The driver is free to pick something near what was asked for, so the configuration is rewritten
        // to what it actually granted rather than to what was requested.
        cfg.size.width = fmt.fmt.pix.width;
        cfg.size.height = fmt.fmt.pix.height;
        cfg.format.fourcc = fmt.fmt.pix.pixelformat;

        ::v4l2_streamparm parm {};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = cfg.fps.numerator;
        parm.parm.capture.timeperframe.denominator = cfg.fps.denominator;
        // Advisory: not every driver implements VIDIOC_S_PARM, and none has to.
        static_cast<void>(::ioctl(fd, VIDIOC_S_PARM, &parm));
        return {};
    }

    capture::expected_mmap_buffers capture::map_buffers(const int fd, capture_config& cfg) noexcept
    {
        ::v4l2_requestbuffers req {};
        req.count = cfg.buffer_count;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (::ioctl(fd, VIDIOC_REQBUFS, &req) < 0)
            return std::unexpected(kmx::aio::from_errno(errno));
        if (req.count == 0u)
            return std::unexpected(kmx::aio::error_code::internal_error);

        cfg.buffer_count = req.count; // the driver may grant fewer buffers than were asked for
        std::vector<mmap_buffer> buffers;
        buffers.reserve(req.count);

        for (std::uint32_t i = 0u; i < req.count; ++i)
        {
            ::v4l2_buffer buf {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            void* ptr = MAP_FAILED; // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
            if (::ioctl(fd, VIDIOC_QUERYBUF, &buf) >= 0)
                ptr = ::mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, static_cast<off_t>(buf.m.offset));
            if (ptr == MAP_FAILED) // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
            {
                // Whatever was mapped before this failure is released here: the caller is handed either a
                // complete set of buffers or none at all.
                unmap_all(buffers);
                return std::unexpected(kmx::aio::from_errno(errno));
            }

            buffers.push_back({ptr, buf.length});
        }
        return buffers;
    }

    std::expected<void, kmx::aio::error_code> capture::enqueue_buffers(const int fd, const std::uint32_t count) noexcept
    {
        // Primes the driver's queue: a capture that starts streaming with nothing queued delivers nothing.
        for (std::uint32_t i = 0u; i < count; ++i)
        {
            ::v4l2_buffer buf {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            if (::ioctl(fd, VIDIOC_QBUF, &buf) < 0)
                return std::unexpected(kmx::aio::from_errno(errno));
        }
        return {};
    }

    capture::create_result capture::create(executor& exec, capture_config cfg) noexcept
    {
        auto fd = open_capture_device(cfg);
        if (!fd.has_value())
            return std::unexpected(fd.error());

        const int raw_fd = fd->get();
        if (const auto negotiated = negotiate_format(raw_fd, cfg); !negotiated.has_value())
            return std::unexpected(negotiated.error());

        auto buffers = map_buffers(raw_fd, cfg);
        if (!buffers.has_value())
            return std::unexpected(buffers.error());

        const auto fail = [&buffers](const kmx::aio::error_code ec) noexcept
        { unmap_all(*buffers); return std::unexpected(ec); };

        if (const auto queued = enqueue_buffers(raw_fd, cfg.buffer_count); !queued.has_value())
            return fail(queued.error());
        if (const auto reg = exec.register_fd(raw_fd); !reg)
            return fail(kmx::aio::from_errno(reg.error().value()));

        const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (::ioctl(raw_fd, VIDIOC_STREAMON, &type) < 0)
        {
            exec.unregister_fd(raw_fd);
            return fail(kmx::aio::from_errno(errno));
        }

        capture result {exec, std::move(*fd), std::move(cfg), std::move(*buffers)};
        result.streaming_ = true;
        return result;
    }

    // capture::~capture()

    void capture::unmap_buffers() noexcept
    {
        for (auto& buf: buffers_)
            if (buf.ptr && (buf.ptr != MAP_FAILED))
                ::munmap(buf.ptr, buf.length);

        buffers_.clear();
    }

    capture::~capture() noexcept
    {
        if (fd_.is_valid() && streaming_)
        {
            const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            (void) ::ioctl(fd_.get(), VIDIOC_STREAMOFF, &type);
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
            if (!co_await exec_.wait_io(fd_.get(), event_type::read))
                co_return std::unexpected(error_code::operation_cancelled);

            ::v4l2_buffer buf {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            if (::ioctl(fd_.get(), VIDIOC_DQBUF, &buf) < 0)
            {
                if (would_block(errno))
                    continue;

                co_return std::unexpected(kmx::aio::from_errno(errno));
            }

            const auto& mapped = buffers_[buf.index];

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

} // namespace kmx::aio::readiness::v4l2
