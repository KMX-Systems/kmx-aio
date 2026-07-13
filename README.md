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
* **HTTP/2**: Full codec, stream, frame, and HPACK serialization stack (no model affinity).
* **[TLS](https://www.rfc-editor.org/rfc/rfc8446)/[ALPN](https://www.rfc-editor.org/rfc/rfc7301)**: Encrypted streams ([BoringSSL](https://boringssl.googlesource.com/boringssl/)-backed, both models) with [Application Layer Protocol Negotiation](https://www.rfc-editor.org/rfc/rfc7301) for seamless HTTP/2 handshakes.
* **[QUIC](https://datatracker.ietf.org/doc/rfc9000/) + [HTTP/3](https://www.rfc-editor.org/rfc/rfc9114)** ⚙: Full QUIC engine (both models) with [lsquic](https://github.com/litespeedtech/lsquic) backing; HTTP/3 server/client samples included.
* **Async Timers**: Readiness timer (`timerfd` + `epoll`) and completion timer (`io_uring` timeout op).
* **[V4L2](https://linuxtv.org) Async Capture** (Readiness + Completion): Readiness mode uses epoll-driven frame capture; completion mode uses `IORING_OP_POLL_ADD` plus synchronous `VIDIOC_DQBUF` (hybrid model) in the same io_uring executor. Targets V4L2 streaming devices such as USB webcams, [MIPI CSI-2](https://www.mipi.org/specifications/csi-2) pipelines, and [GMSL](https://en.wikipedia.org/wiki/Gigabit_Multimedia_Serial_Link) camera chains. Frames land in `co_await`-returned `frame_view` objects that auto-requeue mmap'd kernel buffers on destruction.
* **Completion `async_poll(fd, mask)`**: First-class one-shot `IORING_OP_POLL_ADD` primitive to await arbitrary fd readiness (V4L2, eventfd, timerfd, signalfd, netlink) inside `completion::executor`; callers re-arm by invoking it again.
* **Buffer Pool Primitives**: `kmx::aio::buffer_pool` and `kmx::aio::buffer_handle` provide fixed-capacity RAII buffer leasing for deterministic zero-copy workflows.
* **[Channel Backpressure](https://www.geeksforgeeks.org/computer-networks/back-pressure-in-distributed-systems/)**: `kmx::aio::channel` supports watermark-based producer throttling and credit reporting.
* **[OPC UA](https://en.wikipedia.org/wiki/OPC_Unified_Architecture)** ⚙: Backend-neutral async client/server/subscription facade with [open62541](https://open62541.org) backend support; this repository drives it through completion-executor progression, with a shim fallback for feature-off builds and tests.
* **[SOME/IP](https://www.autosar.org/fileadmin/standards/R22-11/FO/AUTOSAR_PRS_SOMEIPProtocol.pdf)** ⚙: Backend-neutral async client/server/subscription facade for [AUTOSAR](https://www.autosar.org/) SOME/IP communication; [vsomeip](https://github.com/COVESA/vsomeip)-backed when available, with an in-process stub for deterministic unit testing without a daemon.
* **GPU Completion Model ([CUDA](https://developer.nvidia.com/cuda/toolkit))** ⚙: Lightweight thread-per-core `gpu::executor` allowing `co_await` on asynchronous CUDA event completions (`gpu::event`) submitted to CUDA streams (`gpu::stream`).
* **AVB (Audio Video Bridging, [IEEE 802.1](https://1.ieee802.org/avbridges/))**: Shared generic raw [Ethernet](https://en.wikipedia.org/wiki/Ethernet) socket, [gPTP](https://standards.ieee.org/ieee/802.1AS/6047/) clock synchronization, and [SRP](https://standards.ieee.org/ieee/802.1Qat/4041/) client with model-specific aliases for readiness and completion; sample-validated in both models.
* **[HFT](https://en.wikipedia.org/wiki/High-frequency_trading) Order Router Sample**: Completion-sample demo using `kmx::aio::channel` between CPU-pinned threads to show producer throttling and synthetic order routing stats.
* **[AF_XDP Packet Socket](https://www.kernel.org/doc/html/latest/networking/af_xdp.html)** ⚙: Kernel-bypass packet filtering with [eBPF](https://ebpf.io/) support and [UMEM](https://www.kernel.org/doc/html/latest/networking/af_xdp.html#umem) ring management.
* **[SPDK Block I/O](https://spdk.io/)** ⚙: [NVMe](https://nvmexpress.org/), generic [bdev](https://spdk.io/doc/bdev.html), and storage acceleration via [DPDK](https://www.dpdk.org/).

> ⚙ — Feature-gated: requires `project.enable_*:true`; off by default.

## Feature Domain Applicability Matrix

Quick reference that groups features by domain and highlights only their applicability scope.

### Networking Core

| Feature | Applicability domain |
| :--- | :--- |
| [TCP](documentation/features/tcp.md) | Client-server applications requiring reliable byte-stream transport and long-lived connections |
| [UDP Endpoint](documentation/features/udp.md) | High-level datagram API with automatic peer address management |
| [UDP Socket](documentation/features/udp.md) | Low-level datagram transport for custom protocols or best-effort traffic |

### Security and Web Protocols

| Feature | Applicability domain |
| :--- | :--- |
| [HTTP/2](documentation/features/http2.md) | Multiplexed application-layer services over persistent connections |
| [HTTP/3](documentation/features/quic-http3.md) | Modern web services over QUIC with reduced handshake/reconnect latency |
| [QUIC Engine](documentation/features/quic-http3.md) | Secure user-space transport with stream multiplexing and modern congestion control |
| [TLS Stream](documentation/features/tls-http2.md) | End-to-end encrypted channels for secure transport |

### Time, Device IO, and Polling

| Feature | Applicability domain |
| :--- | :--- |
| Completion async_poll(fd, mask) | Unified readiness waiting for arbitrary file descriptors in completion execution |
| [Timers](documentation/features/timers.md) | Time-based scheduling, deadlines, retry/backoff, and periodic work |
| [V4L2 Capture](documentation/features/v4l2.md) | Asynchronous video capture for Linux camera devices and multimedia pipelines |

### Industrial and Automotive Integration

| Feature | Applicability domain |
| :--- | :--- |
| [AVB/IEEE 802.1](documentation/features/avb.md) | Deterministic, synchronized audio/video streaming over real-time Ethernet networks |
| [OPC UA](documentation/features/opc-ua.md) | Industrial interoperability for telemetry, control, and subscriptions in automation systems |
| [SOME/IP](documentation/features/someip.md) | Service-oriented communication for AUTOSAR automotive systems |

### Performance and Hardware Acceleration

| Feature | Applicability domain |
| :--- | :--- |
| [AF_XDP Packets](documentation/features/af-xdp.md) | Kernel-bypass packet processing for low-latency, high-throughput networking |
| [GPU / CUDA](documentation/features/gpu-cuda.md) | Asynchronous GPU offload for compute-bound and post-processing pipelines |
| [OpenOnload](documentation/features/openonload.md) | Accelerated user-space networking for latency-sensitive workloads |
| [SPDK Block I/O](documentation/features/spdk.md) | High-performance storage I/O for NVMe/bdev data-plane scenarios |

### Concurrency and Reference Workloads

| Feature | Applicability domain |
| :--- | :--- |
| [HFT Order Router](documentation/features/hft-order-router.md) | High-pressure producer-consumer flows with CPU pinning and controlled backpressure |

## Feature Availability Matrix

Quick reference showing which APIs are available in each execution model:

| Feature | Readiness (epoll) | Completion (io_uring) | ⚙ | Notes |
| :--- | :---: | :---: | :---: | :--- |
| [**TCP**](documentation/features/tcp.md) | ✅ | ✅ | | Listener, stream, accept, read, write |
| [**UDP Socket**](documentation/features/udp.md) | ✅ | ✅ | | Low-level recvmsg/sendmsg |
| [**UDP Endpoint**](documentation/features/udp.md) | ✅ | ❌ | | High-level span API; completion callers manage `msghdr`/`iovec` directly |
| [**TLS Stream**](documentation/features/tls-http2.md) | ✅ | ✅ | | Generic template; BoringSSL Memory BIO |
| [**Timers**](documentation/features/timers.md) | ✅ | ✅ | | Readiness: timerfd + epoll; Completion: io_uring timeout ops |
| [**AF_XDP Packets**](documentation/features/af-xdp.md) | ❌ | ✅ | ⚙ | Kernel-bypass; eBPF filtering; UMEM ring management |
| [**AVB/IEEE 802.1**](documentation/features/avb.md) | ✅ | ✅ | ⚙ | Shared generic stack; readiness/completion aliases; sample-validated |
| [**GPU / CUDA**](documentation/features/gpu-cuda.md) | ❌ | ✅ | ⚙ | Async CUDA event completion; thread-per-core pinning |
| [**HFT Order Router**](documentation/features/hft-order-router.md) | ❌ | ✅ | ⚙ | Sample demo; `kmx::aio::channel` with CPU pinning |
| [**HTTP/2**](documentation/features/http2.md) | ✅ | ✅ | ⚙ | Full codec + ALPN; no executor affinity |
| [**HTTP/3**](documentation/features/quic-http3.md) | ✅ | ✅ | ⚙ | HTTP/3 codec and message layer over QUIC |
| [**OPC UA**](documentation/features/opc-ua.md) | ✅ | ✅ | ⚙ | Backend-neutral facade; open62541 backend; completion-driven progression |
| [**OpenOnload**](documentation/features/openonload.md) | ✅ | ❌ | ⚙ | Zero-copy extensions; headers-only; gracefully disabled when absent |
| [**QUIC Engine**](documentation/features/quic-http3.md) | ✅ | ✅ | ⚙ | lsquic-backed; HTTP/3 server/client samples |
| [**SOME/IP**](documentation/features/someip.md) | ✅ | ✅ | ⚙ | Backend-neutral facade; vsomeip or stub backend; echo server/client samples |
| [**SPDK Block I/O**](documentation/features/spdk.md) | ❌ | ✅ | ⚙ | NVMe, generic bdev; DPDK-backed |
| [**V4L2 Capture**](documentation/features/v4l2.md) | ✅ | ✅ | ⚙ | Readiness: zero-copy mmap + epoll; Completion: `IORING_OP_POLL_ADD` + `VIDIOC_DQBUF` |

**Legend:**

* ✅ — Available
* ❌ — Not available in this execution model
* ⚙ — Feature-gated (requires `project.enable_*:true`; off by default)

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
