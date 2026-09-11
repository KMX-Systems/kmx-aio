/// @file src/kmx/aio/completion/v4l2/frame_view.cpp
/// @brief Zero-copy V4L2 frame view — completion (io_uring) model implementation: buffer ownership and re-enqueue.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/completion/v4l2/frame_view.hpp>
#ifndef PCH
    #include <kmx/logger.hpp>

    #include <cerrno>
    #include <cstring>
    #include <source_location>
    #include <utility>
    #include <linux/videodev2.h>
    #include <sys/ioctl.h>
#endif

namespace kmx::aio::completion::v4l2
{
    // frame_view

    frame_view::frame_view(dequeued_buffer buffer) noexcept:
        device_fd_(buffer.device_fd),
        index_(buffer.index),
        ptr_(buffer.ptr),
        length_(buffer.length),
        metadata_(buffer.metadata),
        device_lifetime_(std::move(buffer.device_lifetime))
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

    cspan_byte_t frame_view::data() const noexcept
    {
        return {ptr_, metadata_.bytes_used};
    }

}
