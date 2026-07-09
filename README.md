# kmx-aio

**kmx-aio** is a modern, high-performance C++26 asynchronous I/O library designed for building non-blocking network applications on Linux. It leverages C++ coroutines to provide a clean, synchronous-looking API for asynchronous operations across two execution models: readiness (`epoll`) and completion (`io_uring`).

## Key Features

* **Modern C++26**: Built with the latest language standards.
* **Coroutine-First Design**: Uses `co_await` for intuitive, sequential async code flow without callback hell.
* **Readiness + Completion Models**: `epoll`-based readiness and `io_uring`-based completion APIs.
* **Zero-Overhead Abstractions**: Lightweight wrappers around system calls.
* **Type-Safe Error Handling**: Extensive use of `std::expected` and `std::error_code` for robust error management.
* **TCP Networking**: Built-in support for TCP listeners and streams.
* **UDP Networking**: Dual-layer API in readiness model — low-level `readiness::udp::socket` and high-level `readiness::udp::endpoint` with automatic address management. Completion model provides socket-level `completion::udp::socket` only.
* **[TLS](https://www.rfc-editor.org/rfc/rfc8446)/[ALPN](https://www.rfc-editor.org/rfc/rfc7301)**: Encrypted streams ([BoringSSL](https://boringssl.googlesource.com/boringssl/)-backed, both models) with Application Layer Protocol Negotiation for seamless HTTP/2 handshakes.
* **[QUIC](https://datatracker.ietf.org/doc/rfc9000/) + [HTTP/3](https://www.rfc-editor.org/rfc/rfc9114)**: Full QUIC engine (both models) with [lsquic](https://github.com/litespeedtech/lsquic) backing; HTTP/3 server/client samples included.
* **Async Timers**: Readiness timer (`timerfd` + `epoll`) and completion timer (`io_uring` timeout op).
* **[V4L2](https://linuxtv.org) Async Capture** (Readiness + Completion): Readiness mode uses epoll-driven frame capture; completion mode uses `IORING_OP_POLL_ADD` plus synchronous `VIDIOC_DQBUF` (hybrid model) in the same io_uring executor. Targets V4L2 streaming devices such as USB webcams, MIPI CSI-2 pipelines, and GMSL camera chains. Frames land in `co_await`-returned `frame_view` objects that auto-requeue mmap'd kernel buffers on destruction.
* **Completion `async_poll(fd, mask)`**: First-class one-shot `IORING_OP_POLL_ADD` primitive to await arbitrary fd readiness (V4L2, eventfd, timerfd, signalfd, netlink) inside `completion::executor`; callers re-arm by invoking it again.
* **Buffer Pool Primitives**: `kmx::aio::buffer_pool` and `kmx::aio::buffer_handle` provide fixed-capacity RAII buffer leasing for deterministic zero-copy workflows.
* **[Channel Backpressure](https://www.geeksforgeeks.org/computer-networks/back-pressure-in-distributed-systems/)**: `kmx::aio::channel` supports watermark-based producer throttling and credit reporting.
* **HTTP/2**: Full codec, stream, frame, and HPACK serialization stack (no model affinity).
* **[OPC UA](https://en.wikipedia.org/wiki/OPC_Unified_Architecture)** (feature-gated): Backend-neutral async client/server/subscription facade with [open62541](https://open62541.org) backend support; this repository drives it through completion-executor progression, with a shim fallback for feature-off builds and tests.
* **GPU Completion Model ([CUDA](https://developer.nvidia.com/cuda/toolkit))** (feature-gated): Lightweight thread-per-core `gpu::executor` allowing `co_await` on asynchronous CUDA event completions (`gpu::event`) submitted to CUDA streams (`gpu::stream`).
* **AVB (Audio Video Bridging, [IEEE 802.1](https://1.ieee802.org/avbridges/))**: Shared generic raw Ethernet socket, gPTP clock synchronization, and SRP client with model-specific aliases for readiness and completion; sample-validated in both models.
* **[HFT](https://en.wikipedia.org/wiki/High-frequency_trading) Order Router Sample**: Completion-sample demo using `kmx::aio::channel` between CPU-pinned threads to show producer throttling and synthetic order routing stats.
* **[AF_XDP Packet Socket](https://www.kernel.org/doc/html/latest/networking/af_xdp.html)** (Completion model, gated): Kernel-bypass packet filtering with eBPF support and UMEM ring management.
* **[SPDK Block I/O](https://spdk.io/)** (Completion model, gated): NVMe, generic bdev, and storage acceleration via DPDK.

## Feature Availability Matrix

Quick reference showing which APIs are available in each execution model:

| Feature | Readiness (epoll) | Completion (io_uring) | Notes |
| --------- | ------ | ------ | ------- |
| **TCP** | ✅ Full | ✅ Full | Listener, stream, accept, read, write |
| **UDP Socket** | ✅ Full | ✅ Full | Low-level recvmsg/sendmsg |
| **UDP Endpoint** | ✅ High-level span API | ❌ Not available | Completion users manage `msghdr`/`iovec` directly |
| [**TLS Stream**](documentation/features/tls-http2.md) | ✅ Full | ✅ Full | Generic template; BoringSSL Memory BIO |
| [**QUIC Engine**](documentation/features/quic-http3.md) | ✅ Full (lsquic) | ✅ Full (lsquic) | HTTP/3 server/client; feature-gated |
| **Timers** | ✅ timerfd + epoll | ✅ io_uring timeout ops | Periodic and one-shot |
| [**V4L2 Capture**](documentation/features/v4l2.md) | ✅ Zero-copy mmap + epoll | ✅ Hybrid poll + DQBUF | Completion uses `IORING_OP_POLL_ADD`, then `VIDIOC_DQBUF` |
| **HTTP/2** | ✅ Codec + ALPN | ✅ Codec + ALPN | No executor affinity |
| [**HTTP/3**](documentation/features/quic-http3.md) | ✅ Full | ✅ Full | HTTP/3 codec and message layer over QUIC |
| [**AVB/IEEE 802.1**](documentation/features/avb.md) | ✅ Available | ✅ Available | Shared generic AVB stack with readiness/completion aliases; sample-validated in both models |
| [**AF_XDP Packets**](documentation/features/af-xdp.md) | ❌ Not available | ✅ Kernel-bypass (gated) | Feature-gated; eBPF packet filtering |
| [**SPDK Block I/O**](documentation/features/spdk.md) | ❌ Not available | ✅ NVMe/bdev (gated) | Feature-gated; DPDK-backed |
| **OpenOnload** | ✅ Zero-copy extensions | ❌ Not available | Readiness-only; headers-only; gracefully disabled |
| [**OPC UA**](documentation/features/opc-ua.md) | ✅ (feature-gated) | ✅ (feature-gated) | Backend-neutral facade; progression is completion-driven here; disabled by default |
| [**GPU / CUDA**](documentation/features/gpu-cuda.md) | ❌ Not available | ✅ Full (feature-gated) | Async events resumption, thread-per-core pinning |
| [**HFT Order Router**](documentation/features/hft-order-router.md) | ❌ Not available | ✅ Sample demo | Completion sample using `kmx::aio::channel` and CPU pinning |

**Legend:**

* ✅ Available / Fully implemented
* ❌ Not available in this model
* ⚙ Feature-gated — gate-controlled via `project.enable_*`; default active graph is `core + completion + quic`; readiness, HTTP/2, HTTP/3, AVB, AF_XDP, SPDK, OpenOnload, CUDA, and OPC UA are off by default
* Headers-only = API declared but no implementation; graceful degradation at runtime

## Documentation

* [Architecture](documentation/architecture.md)
* [Core Primitives](documentation/features/core-primitives.md)
* [Readiness Model (epoll)](documentation/features/readiness-epoll.md)
* [Completion Model (io_uring)](documentation/features/completion-io-uring.md)
* [Known Limitations](documentation/known-limitations.md)
* [Setup and Dependencies](documentation/setup.md)
* [Build and Feature Gates](documentation/build.md)
* [Testing Workflow](documentation/testing.md)
* [Static Analysis](documentation/static-analysis.md)

## License

Copyright &copy; 2026 - present KMX Systems. All rights reserved.
