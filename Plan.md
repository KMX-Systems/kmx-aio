# KMX AIO Next-Gen Architecture Plan

This document outlines the architectural enhancements required to add massive performance scalability to `kmx-aio` using `io_uring`, Kernel Bypass technologies (AF_XDP/OpenOnload/SPDK), TCP/TLS, and QUIC.

## User Review Required

> [!IMPORTANT]
> Based on feedback, the `kmx::aio::common` namespace has been eliminated. All shared C++20 I/O primitives now reside directly in the root `kmx::aio` namespace to simplify the API surface.

## Proposed Namespace Restructuring

To prevent code duplication and ensure interoperability, KMX AIO is tiered into four conceptual pillars: the **Root Primitives**, the **Readiness** execution engine (epoll/bypass), the **Completion** execution engine (io_uring/UMEM), and the **GPU Completion** execution engine (CUDA streams/events).

```cpp
// ===============================================
// 0. The Root Primitives (kmx::aio)
// ===============================================
// Completely agnostic to how the events are polled.

kmx::aio::buffer             // Memory views (spans, ring slices)
kmx::aio::error_code         // Standardized networked errors
kmx::aio::task               // The base C++20 coroutine wrapper
kmx::aio::allocator          // The lockless Slab allocator traits
kmx::aio::channel            // The lockless SPSC cross-thread queues
kmx::aio::stream_reader      // The polymorphic interface trait
kmx::aio::stream_writer      // The polymorphic interface trait
kmx::aio::buffer_pool        // Fixed-capacity ownership-tracked buffer pools
kmx::aio::buffer_handle      // Move-only RAII handle leasing pooled buffers


// ===============================================
// 1. The Readiness Model (Zero-Buffer-Commit)
// ===============================================
// Triggered by epoll, OpenOnload. Wakes coroutine to call recv().

kmx::aio::readiness::executor          
kmx::aio::readiness::tcp::stream
kmx::aio::readiness::tcp::listener
kmx::aio::readiness::udp::socket
kmx::aio::readiness::v4l2::capture     // V4L2 MMAP streaming (epoll-readiness)
kmx::aio::readiness::openonload::      // OpenOnload kernel-bypass TCP/UDP

kmx::aio::readiness::tls::stream       
kmx::aio::readiness::quic::engine      


// ===============================================
// 2. The Completion Model (Fixed Memory Buffers)
// ===============================================
// Triggered by io_uring, AF_XDP, SPDK. Wakes coroutine when OS/NIC has filled a buffer.

kmx::aio::completion::executor         
kmx::aio::completion::tcp::stream      
kmx::aio::completion::tcp::listener    
kmx::aio::completion::udp::socket
kmx::aio::completion::xdp::socket      
kmx::aio::completion::v4l2::capture    // V4L2 MMAP streaming (IORING_OP_POLL_ADD hybrid)
kmx::aio::completion::avb::eth_socket  // AF_PACKET raw Ethernet for IEEE 802.1Qav
kmx::aio::completion::avb::srp::client // Stream Reservation Protocol (MSRP/MVRP)
kmx::aio::completion::avb::gptp::clock // IEEE 802.1AS gPTP hardware clock discipline

kmx::aio::completion::tls::stream      
kmx::aio::completion::quic::engine     


// ===============================================
// 3. The GPU Completion Model (CUDA Streams)
// ===============================================
// Triggered by CUDA stream/event completion. Wakes coroutine when GPU work is done.

kmx::aio::gpu::executor
kmx::aio::gpu::stream
kmx::aio::gpu::event
```

### New Async-I/O Core Components (Scope-Strict)

To keep KMX-AIO focused exclusively on asynchronous I/O substrate concerns, we add exactly three low-level mechanisms:

1. **CUDA Stream Executor** (`kmx::aio::gpu::executor`)
  - Adds a third completion-style execution model for GPU event-driven continuations.
  - Provides coroutine resumption on CUDA stream/event completion.
  - Excludes inference orchestration and model lifecycle policy.

2. **Buffer Pool Management** (`kmx::aio::buffer_pool`, `kmx::aio::buffer_handle`)
  - Adds fixed-capacity preallocated pools with deterministic ownership and RAII reclamation.
  - Supports zero-copy and fixed-buffer I/O workflows.
  - Excludes domain-level memory policies (health budgets, failover behavior).

3. **Channel Backpressure** (`kmx::aio::channel` extensions)
  - Adds watermark and credit-based producer throttling mechanisms.
  - Preserves non-blocking SPSC operation semantics.
  - Excludes scheduling policy and SLO-driven adaptation logic.

---

## Deeper Architectural Abstractions

Beyond establishing the layout of the namespaces, we must define **how** components interact across them without sacrificing performance.

### 1. Interface Polymorphism 
If an application developer writes an HTTP parser, they should not care if the underlying bytes arrive via `io_uring`, `epoll`, `BoringSSL`, or a mock testing socket.

**The Solution:** 
We mandate standard concepts (or virtual interfaces) in the root namespace that all transport and security overlays satisfy.
- `kmx::aio::stream_reader`: Mandates a `task<...>` returning bytes.
- `kmx::aio::stream_writer`: Mandates a `task<...>` consuming bytes.

By adhering to this, `kmx::aio::completion::tcp::stream` and `kmx::aio::readiness::tls::stream` are completely interchangeable downstream.

### 2. Zero-Cost Error Propagation (`std::expected` vs Exceptions)
In high-frequency networking, dropped packets, peer resets, and timeouts are standard control flow, not exceptional circumstances. C++ exceptions (`throw`) involve unwinding the stack, which is devastating to latency.

**The Solution:** 
KMX AIO coroutines **must not** throw exceptions on network errors. Every asynchronous operation must strictly return a `std::expected<Type, kmx::aio::error_code>`. 
```cpp
// Correct (Zero-cost branch predictable)
kmx::aio::task<std::expected<size_t, kmx::aio::error_code>> read(span<char> buf);
```

---

## Pillar 1: Kernel Bypass & Storage Technologies
1. **`OpenOnload` (Transparent Bypass for HFT)**: `kmx::aio::readiness` 
2. **`AF_XDP` (Raw Packet Processing for NFV)**: `kmx::aio::completion`
3. **`SPDK` (NVMe-oF Storage)**: `kmx::aio::completion`

---

## Pillar 2: Standard TCP/IP & BoringSSL (TLS 1.3)
1. **`kmx::aio::readiness::tls::stream`**
2. **`kmx::aio::completion::tls::stream`**
*Isolated via BoringSSL Memory BIOs.*

---

## Pillar 3: UDP & lsquic (QUIC / HTTP3)
1. **`kmx::aio::readiness::quic::engine`**
2. **`kmx::aio::completion::quic::engine`**
*Routing multiplexed QUIC streams to their respective connection handlers.*

---

## Pillar 4: V4L2 Device Capture
Captures raw video frames from V4L2 camera devices under both execution models.

**Architecture:** V4L2 has no native io_uring submission command. The completion model
uses a **hybrid approach**: `IORING_OP_POLL_ADD` waits for `POLLIN` on the V4L2 file
descriptor; once the CQE fires, `VIDIOC_DQBUF` is called synchronously (always fast at
that point). The buffer is wrapped in a move-only `frame_view` RAII type; its destructor
calls `VIDIOC_QBUF` to re-enqueue the buffer automatically.

**Value proposition:** Both `completion::v4l2::capture` and `completion::timer` (or any
TCP/UDP socket) share the *same* `completion::executor` and the *same* io_uring ring.
No separate epoll instance is required. This is the canonical demonstration of the
completion model's composability.

1. **`kmx::aio::readiness::v4l2::capture`** — epoll + synchronous DQBUF (implemented)
2. **`kmx::aio::completion::v4l2::capture`** — `IORING_OP_POLL_ADD` + synchronous DQBUF (implemented)

---

## Pillar 5: AVB / IEEE 802.1 (Audio/Video Bridging)
Realtime A/V stream transport over Ethernet using the IEEE 802.1Q suite.

**Relevant standards:** 802.1AS (gPTP clock sync), 802.1Qav (Credit-Based Shaper),
802.1Qat/MSRP (Stream Reservation Protocol), 61883-6 (AM824 audio framing).

**Architecture:** Uses raw `AF_PACKET` sockets (or AF_XDP for zero-copy Tx) bound to a
specific Ethernet interface. All operations (send, recv, timer for presentation clock)
run in one `completion::executor` ring.

1. **`kmx::aio::completion::avb::eth_socket`** — raw Ethernet Tx/Rx
2. **`kmx::aio::completion::avb::srp::client`** — MSRP/MVRP stream reservation
3. **`kmx::aio::completion::avb::gptp::clock`** — IEEE 802.1AS hardware clock discipline

---

## Deeper Architectural Abstractions (Extended)

### 3. Execution & Threading Model (Thread-Per-Core)
For maximum performance with technologies like `io_uring` and `AF_XDP`, a **thread-per-core (share-nothing)** architecture is heavily preferred.
- `kmx::aio::readiness::executor` and `kmx::aio::completion::executor` instances will pin themselves to specific CPU cores.
- State is completely local to the executing thread.
- Cross-thread communication is handled exclusively via lockless `kmx::aio::channel` queues, avoiding mutex contention entirely.

### 4. Memory Management & Zero-Copy Lifecycles
Buffer lifecycles in completion-based models (like `io_uring` and `AF_XDP` UMEM) require strict ownership rules.
- **Ownership:** When a read is submitted to the kernel, the kernel owns the buffer. The application cannot touch it until the completion event fires.
- **Pre-registered Buffers:** `kmx::aio::completion::executor` will negotiate fixed, pre-registered memory buffers with the kernel (`IORING_OP_PROVIDE_BUFFERS` / `IORING_REGISTER_BUFFERS`) to bypass the OS memory management overhead.
- **Slab Allocation:** The `kmx::aio::allocator` will provide lockless, thread-local, cache-aligned slab allocation for fast task and payload construction.
- **Device-Backed Memory Constraint:** V4L2 MMAP buffers are file-backed (`mmap(MAP_SHARED, v4l2_fd, ...)`). The kernel rejects `io_uring_register_buffers()` for such pages with `EOPNOTSUPP`. Zero-copy camera-to-network therefore requires the V4L2 DMABUF or USERPTR pathway (out of scope for this plan phase).

### 5. Coroutine Scheduling & Resumption
- **Cancellation:** In-flight kernel requests cannot simply be dropped. If a timeout occurs, the coroutine must gracefully issue a cancellation request (e.g., `IORING_OP_ASYNC_CANCEL`) and await the aborted completion before destroying the execution frame. `std::stop_token` integration will be standardized across `kmx::aio::task`.

### 6. Testing & Benchmarking Strategy
- **Mock Interfaces:** By adhering to `kmx::aio::stream_reader` and `kmx::aio::stream_writer`, parsers and business logic can be unit-tested entirely offline without instantiating network layers.
- **Benchmarking:** Core latency will be measured via thread-pinned ping-pong tests against `Boost.Asio` and standard `epoll`. Max throughput tests will evaluate NUMA-aware ring buffer saturation.
1. **`kmx::aio::readiness::quic::engine`**
2. **`kmx::aio::completion::quic::engine`**
*Isolated via lsquic User-Space APIs.*

---

## Critical Architecture Review (What We Missed)
1. **Unified Time & Timers**: Introduce `kmx::aio::readiness::timer` and `kmx::aio::completion::timer`. Both expose a unified C++20 `co_await timer.wait()`.
2. **NUMA and CPU Core Affinity**: Executors must support **Core Pinning** construction, e.g., `readiness::executor(core_id = 4)` or `completion::executor(core_id = 4)`. A coroutine spawned on `executor 4` must *always* `co_resume` on core 4.
3. **Custom Memory Allocators** (`kmx::aio::allocator`): Mandate `std::coroutine_traits` overrides to route coroutine frame allocations to a thread-local, lockless fixed-size Slab Allocator.
4. **Cross-Thread Dispatch** (`kmx::aio::channel`): Implement Single-Producer Single-Consumer (SPSC) lock-free ring-buffer channels.
5. **`async_poll(fd, mask)`** — First-class `IORING_OP_POLL_ADD` primitive on `completion::executor`. Enables any file descriptor (V4L2, GPIO, netlink, eventfd, etc.) to suspend a coroutine in the same io_uring ring as TCP/UDP/timer operations without a separate epoll instance. One-shot only; callers re-arm by calling again (matches the executor's no-persistent-subscription model).
---

## Prior Art & Architectural Advantages
1. **vs. Seastar (by ScyllaDB)**: By strictly leveraging **native C++20 compiler-generated coroutines** (`kmx::aio::task`), KMX AIO achieves Seastar's "Shared-Nothing" NUMA scaling while remaining infinitely more readable than Seastar's proprietary futures.
2. **vs. Boost.Asio (`std::net`)**: KMX AIO binds strictly to Linux primitives (`readiness` and `completion`), avoiding Asio's famously dense, OS-agnostic template bloat. KMX correctly models Asio's brilliant decoupled TLS abstraction.
3. **vs. libunifex / `std::execution` (Meta)**: Unlike `std::execution`'s abstract P2300 nodes, KMX AIO's `co_await` paradigm provides a standard, linear, synchronous-looking control flow while preserving identical performance.

---

## Sample Application Roadmap

To incrementally validate the architecture against legacy applications (and prove the performance gains), we must implement a strict progression of sample applications in `/source/sample/`.

We will structure the sample development into **Eight Validation Phases (0-7)**, beginning with protecting the existing codebase.

### Phase 0: The Backward Compatibility Migration (Baseline) *(implemented)*
**Goal:** Migrate the existing `kmx-aio` sample ecosystem without breaking API intuition.
- **Task**: The current implementation of `kmx::aio::tcp::stream` and `executor` (which uses `epoll`) must be safely relocated under the `kmx::aio::readiness` namespace.
- **Affected Samples**: tcp/echo, udp/echo, tls/echo, quic
- **Validation**: All existing samples must compile and run identically.

### Phase 1: The Completion Validation (TCP/UDP Echo) *(implemented)*
**Goal:** Prove `io_uring` multishot outperforms `epoll` using naked sockets.

### Phase 2: The Cryptography Validation (BoringSSL) *(implemented)*
**Goal:** Prove the Memory BIO decoupling architecture works across both namespaces.

### Phase 3: The Protocol Overlay Validation (lsquic) *(implemented)*
**Goal:** Prove KMX AIO handles dense state-machines over datagrams.

### Phase 4: The Hardware Bypass Validation (AF_XDP) *(implemented)*
**Goal:** Prove KMX AIO can execute NFV workloads via UMEM rings.

### Phase 5: The Control-Plane Validation (SPSC / CPU Pinning) *(implemented)*
**Goal:** Prove the execution environment is truly lockless for HFT.
- `sample/hft/order_router`: An application with an `io_uring` market-data thread (Core 2) tied directly to a strategy thread (Core 4) via `kmx::aio::channel<Order>`.

### Phase 6: V4L2 Completion Validation
**Goal:** Demonstrate one `completion::executor` driving camera capture and a periodic timer simultaneously, proving the composability advantage over the readiness model.
- `sample/completion/v4l2/capture`: Opens a V4L2 device, streams YUYV frames via `IORING_OP_POLL_ADD + VIDIOC_DQBUF`, while a `completion::timer` coroutine prints per-second throughput stats — all in one io_uring ring with no epoll.
- Compare against `sample/readiness/v4l2/capture` to show equivalent frame throughput with better integration story.
- **Validation metric**: Both capture coroutine and timer coroutine are dispatched on the same executor instance; no secondary event loop is created.

### Phase 7: AVB / IEEE 802.1 Validation
**Goal:** Validate realtime A/V stream transport under `completion::executor` deadline constraints.
- `sample/completion/avb/talker`: Sends AM824-encapsulated audio frames at 48 kHz using `completion::avb::eth_socket` + `completion::avb::gptp::clock` for presentation-time discipline.
- `sample/completion/avb/listener`: Receives and reconstructs the audio stream, measuring jitter against the gPTP presentation clock.
- **Validation metric**: End-to-end jitter < 125 µs (IEEE 802.1Qav Class A requirement) measured by correlating gPTP timestamps against `CLOCK_TAI`.
