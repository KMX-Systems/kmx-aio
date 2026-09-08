/// @file aio/aio.hpp
/// @brief Master include header for the KMX AIO library.
/// @details Provides a single-include entry point for the entire KMX AIO API: the root primitives,
///          both I/O models, and every protocol, security and hardware family the library carries.
///
///          There are no feature guards here, deliberately. Each header below is guarded at its own
///          top on the features it needs and compiles to nothing without them, so what this file
///          expands to already follows the build it is compiled in. Repeating those conditions here
///          would put each of them in two places, and the copy here is the one that would be
///          forgotten - which is how this header came to name only part of the API in the first
///          place. Including a header whose feature is off costs nothing: the public headers pull in
///          no third-party declarations, so a disabled family expands to an empty file.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#ifndef PCH
    // Root primitives
    #include <kmx/aio/async_mutex.hpp>
    #include <kmx/aio/basic_channel.hpp>
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/channel.hpp>
    #include <kmx/aio/error_code.hpp>
    #include <kmx/aio/exception.hpp>
    #include <kmx/aio/executor_base.hpp>
    #include <kmx/aio/file_descriptor.hpp>
    #include <kmx/aio/ipv4.hpp>
    #include <kmx/aio/ipv6.hpp>
    #include <kmx/aio/mac.hpp>
    #include <kmx/aio/scheduler.hpp>
    #include <kmx/aio/stream_concepts.hpp>
    #include <kmx/aio/task.hpp>

    // Allocators and buffers
    #include <kmx/aio/allocator/counter.hpp>
    #include <kmx/aio/allocator/slab.hpp>
    #include <kmx/aio/allocator/statistics.hpp>
    #include <kmx/aio/buffer/handle.hpp>
    #include <kmx/aio/buffer/pool.hpp>
    #include <kmx/aio/buffer/view/item.hpp>

    // TLS
    #include <kmx/aio/tls/basic_stream.hpp>
    #include <kmx/aio/tls/stream.hpp>

    // Readiness model (kmx::aio::readiness)
    #include <kmx/aio/readiness/avb/eth_socket.hpp>
    #include <kmx/aio/readiness/avb/gptp/clock.hpp>
    #include <kmx/aio/readiness/avb/srp/client.hpp>
    #include <kmx/aio/readiness/basic_types.hpp>
    #include <kmx/aio/readiness/descriptor/epoll.hpp>
    #include <kmx/aio/readiness/descriptor/timer.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/io_base.hpp>
    #include <kmx/aio/readiness/knx/client.hpp>
    #include <kmx/aio/readiness/knx/gateway.hpp>
    #include <kmx/aio/readiness/knx/server.hpp>
    #include <kmx/aio/readiness/knx/udp_transport.hpp>
    #include <kmx/aio/readiness/openonload/extensions.hpp>
    #include <kmx/aio/readiness/quic/engine.hpp>
    #include <kmx/aio/readiness/tcp/listener.hpp>
    #include <kmx/aio/readiness/tcp/stream.hpp>
    #include <kmx/aio/readiness/timer.hpp>
    #include <kmx/aio/readiness/tls/stream.hpp>
    #include <kmx/aio/readiness/udp/endpoint.hpp>
    #include <kmx/aio/readiness/udp/socket.hpp>
    #include <kmx/aio/readiness/v4l2/capture.hpp>
    #include <kmx/aio/readiness/v4l2/v4l2_types.hpp>

    // Completion model (kmx::aio::completion)
    #include <kmx/aio/completion/avb/eth_socket.hpp>
    #include <kmx/aio/completion/avb/gptp/clock.hpp>
    #include <kmx/aio/completion/avb/srp/client.hpp>
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/completion/io_base.hpp>
    #include <kmx/aio/completion/knx/client.hpp>
    #include <kmx/aio/completion/knx/gateway.hpp>
    #include <kmx/aio/completion/knx/server.hpp>
    #include <kmx/aio/completion/knx/udp_transport.hpp>
    #include <kmx/aio/completion/quic/engine.hpp>
    #include <kmx/aio/completion/spdk/device.hpp>
    #include <kmx/aio/completion/spdk/runtime.hpp>
    #include <kmx/aio/completion/tcp/listener.hpp>
    #include <kmx/aio/completion/tcp/stream.hpp>
    #include <kmx/aio/completion/timer.hpp>
    #include <kmx/aio/completion/tls/stream.hpp>
    #include <kmx/aio/completion/udp/endpoint.hpp>
    #include <kmx/aio/completion/udp/socket.hpp>
    #include <kmx/aio/completion/v4l2/capture.hpp>
    #include <kmx/aio/completion/xdp/socket.hpp>

    // Protocol, security and hardware families
    #include <kmx/aio/avb/avb_types.hpp>
    #include <kmx/aio/avb/avtp/am824.hpp>
    #include <kmx/aio/avb/eth_socket.hpp>
    #include <kmx/aio/avb/gptp/clock.hpp>
    #include <kmx/aio/avb/srp/client.hpp>
    #include <kmx/aio/gpu/basic_types.hpp>
    #include <kmx/aio/gpu/event.hpp>
    #include <kmx/aio/gpu/executor.hpp>
    #include <kmx/aio/gpu/stream.hpp>
    #include <kmx/aio/http2/codec.hpp>
    #include <kmx/aio/http2/frame.hpp>
    #include <kmx/aio/http2/hpack.hpp>
    #include <kmx/aio/http2/stream.hpp>
    #include <kmx/aio/http3/alpn.hpp>
    #include <kmx/aio/http3/codec.hpp>
    #include <kmx/aio/http3/control.hpp>
    #include <kmx/aio/http3/frame.hpp>
    #include <kmx/aio/http3/message.hpp>
    #include <kmx/aio/http3/qpack.hpp>
    #include <kmx/aio/http3/settings.hpp>
    #include <kmx/aio/http3/stream.hpp>
    #include <kmx/aio/knx/address.hpp>
    #include <kmx/aio/knx/cemi.hpp>
    #include <kmx/aio/knx/client.hpp>
    #include <kmx/aio/knx/connection.hpp>
    #include <kmx/aio/knx/contract.hpp>
    #include <kmx/aio/knx/datagram.hpp>
    #include <kmx/aio/knx/discovery.hpp>
    #include <kmx/aio/knx/dpt.hpp>
    #include <kmx/aio/knx/error.hpp>
    #include <kmx/aio/knx/frame.hpp>
    #include <kmx/aio/knx/gateway.hpp>
    #include <kmx/aio/knx/keyring.hpp>
    #include <kmx/aio/knx/routing.hpp>
    #include <kmx/aio/knx/secure.hpp>
    #include <kmx/aio/knx/server.hpp>
    #include <kmx/aio/knx/session.hpp>
    #include <kmx/aio/knx/transport.hpp>
    #include <kmx/aio/modbus/client.hpp>
    #include <kmx/aio/modbus/error.hpp>
    #include <kmx/aio/modbus/frame.hpp>
    #include <kmx/aio/modbus/server.hpp>
    #include <kmx/aio/modbus/tls_client.hpp>
    #include <kmx/aio/modbus/tls_server.hpp>
    #include <kmx/aio/modbus/types.hpp>
    #include <kmx/aio/opc_ua/client.hpp>
    #include <kmx/aio/opc_ua/error.hpp>
    #include <kmx/aio/opc_ua/server.hpp>
    #include <kmx/aio/opc_ua/subscription.hpp>
    #include <kmx/aio/opc_ua/types.hpp>
    #include <kmx/aio/quic/engine.hpp>
    #include <kmx/aio/quic/settings.hpp>
    #include <kmx/aio/quic/transport.hpp>
    #include <kmx/aio/someip/client.hpp>
    #include <kmx/aio/someip/error.hpp>
    #include <kmx/aio/someip/server.hpp>
    #include <kmx/aio/someip/subscription.hpp>
    #include <kmx/aio/someip/types.hpp>

    // Logging
    #include <kmx/logger.hpp>
#endif
