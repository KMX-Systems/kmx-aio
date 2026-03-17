/// @file aio/aio.hpp
/// @brief Master include header for the KMX AIO library.
/// @details Provides a single-include entry point for the entire KMX AIO API,
///          spanning root primitives, readiness (epoll), and completion (io_uring) models.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

// Root Primitives (kmx::aio)
#include <kmx/aio/allocator.hpp>
#include <kmx/aio/basic_types.hpp>
#include <kmx/aio/buffer.hpp>
#include <kmx/aio/channel.hpp>
#include <kmx/aio/error_code.hpp>
#include <kmx/aio/executor.hpp>
#include <kmx/aio/io_base.hpp>
#include <kmx/aio/scheduler.hpp>
#include <kmx/aio/stream_concepts.hpp>
#include <kmx/aio/task.hpp>

// Readiness Model (kmx::aio::readiness)
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/tcp/listener.hpp>
#include <kmx/aio/readiness/tcp/stream.hpp>
#include <kmx/aio/readiness/timer.hpp>
#include <kmx/aio/readiness/udp/socket.hpp>

// Readiness Model Security/Protocol Overlays
#include <kmx/aio/readiness/tls/stream.hpp>
#include <kmx/aio/readiness/quic/engine.hpp>

// Completion Model (kmx::aio::completion)
#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/tcp/listener.hpp>
#include <kmx/aio/completion/tcp/stream.hpp>
#include <kmx/aio/completion/timer.hpp>
#include <kmx/aio/completion/udp/socket.hpp>

// Completion Model Security/Protocol/HW Overlays
#include <kmx/aio/completion/tls/stream.hpp>
#include <kmx/aio/completion/quic/engine.hpp>
#include <kmx/aio/completion/xdp/socket.hpp>

// Descriptors (Internal / Low-Level)
#include <kmx/aio/descriptor/epoll.hpp>
#include <kmx/aio/descriptor/file.hpp>
#include <kmx/aio/descriptor/timer.hpp>

// Logging
#include <kmx/logger.hpp>
