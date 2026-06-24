# kmx-aio

**kmx-aio** is a modern, high-performance C++26 asynchronous I/O library designed for building non-blocking network applications on Linux. It leverages C++20 coroutines to provide a clean, synchronous-looking API for asynchronous operations across two execution models: readiness (`epoll`) and completion (`io_uring`).

## Key Features

* **Modern C++26**: Built with the latest language standards.
* **Coroutine-First Design**: Uses `co_await` for intuitive, sequential async code flow without callback hell.
* **Readiness + Completion Models**: `epoll`-based readiness and `io_uring`-based completion APIs.
* **Zero-Overhead Abstractions**: Lightweight wrappers around system calls.
* **Type-Safe Error Handling**: Extensive use of `std::expected` and `std::error_code` for robust error management.
* **TCP Networking**: Built-in support for TCP listeners and streams.
* **UDP Networking**: Dual-layer API in readiness model — low-level `readiness::udp::socket` and high-level `readiness::udp::endpoint` with automatic address management. Completion model provides socket-level `completion::udp::socket` only.
* **TLS/ALPN**: Encrypted streams (BoringSSL-backed, both models) with Application Layer Protocol Negotiation for seamless HTTP/2 handshakes.
* **QUIC + HTTP/3**: Full QUIC engine (both models) with lsquic backing; HTTP/3 server/client samples included.
* **Async Timers**: Readiness timer (`timerfd` + `epoll`) and completion timer (`io_uring` timeout op).
* **Async V4L2 Capture** (Readiness + Completion): Readiness mode uses epoll-driven frame capture; completion mode uses `IORING_OP_POLL_ADD` plus synchronous `VIDIOC_DQBUF` (hybrid model) in the same io_uring executor. Frames land in `co_await`-returned `frame_view` objects that auto-requeue mmap'd kernel buffers on destruction.
* **Completion `async_poll(fd, mask)`**: First-class one-shot `IORING_OP_POLL_ADD` primitive to await arbitrary fd readiness (V4L2, eventfd, timerfd, signalfd, netlink) inside `completion::executor`; callers re-arm by invoking it again.
* **Buffer Pool Primitives**: `kmx::aio::buffer_pool` and `kmx::aio::buffer_handle` provide fixed-capacity RAII buffer leasing for deterministic zero-copy workflows.
* **Channel Backpressure**: `kmx::aio::channel` now supports watermark-based producer throttling and credit reporting.
* **HTTP/2**: Full codec, stream, frame, and HPACK serialization stack (no model affinity).
* **AVB (Audio Video Bridging, IEEE 802.1)** (Completion model): Raw Ethernet socket with hardware timestamping, gPTP clock synchronization, and SRP client for stream reservation.
* **AF_XDP Packet Socket** (Completion model, gated): Kernel-bypass packet filtering with eBPF support and UMEM ring management.
* **SPDK Block I/O** (Completion model, gated): NVMe, generic bdev, and storage acceleration via DPDK.

## Requirements

* **Operating System**: Linux (requires `sys/epoll.h`).
* **Compiler**: A C++ compiler supporting C++26 features (e.g., recent GCC or Clang).
* **Build System**: QBS (Qt Build Suite).

## Dependencies

### Runtime / System Dependencies

* **Linux kernel interfaces**: `epoll`, sockets, `timerfd`, and `io_uring` (via `liburing`).
* **POSIX networking**: `arpa/inet.h`, `netinet/in.h`, and related socket APIs.
* **liburing** (required for completion model): `liburing-dev` package provides headers and runtime for io_uring async I/O.

### Build Dependencies

* **QBS**: used to configure and build all products (`qbs` CLI).
* **C++26 toolchain**: compiler and standard library with support for coroutines and modern library features used by this project (for example `std::expected`, `std::span`, and `std::variant`).

### Test-Only Dependencies

* **Catch2**: required only for `kmx-aio-test` (linked as `Catch2Main` and `Catch2` in `source/library-test/unit-test.qbs`).

### Third-Party Runtime Libraries

* **None** beyond the standard C/C++ runtime and Linux system libraries.

### Optional Pillar 1 Prerequisites

These are only required when the corresponding feature gate is enabled (which is the default).
Disable the gate if you lack a dependency.

* **QUIC / HTTP/3** (enabled by default): BoringSSL and lsquic libraries. Build both via `build/install_lsquic.sh` (see below).
* **AF_XDP** (enabled by default): `libbpf-dev`, `libxdp-dev`, `libelf-dev`, `zlib1g-dev`, `clang`, `llvm`.
* **SPDK** (enabled by default): `libaio-dev`, `libnuma-dev`, `uuid-dev`, `meson`, `ninja-build`, and SPDK runtime libraries.
* **AVB / IEEE 802.1** (Completion model, enabled by default): Kernel with PTP/hardware timestamping support (most modern NIC drivers); user must have `CAP_NET_RAW` privilege.
* **OpenOnload** (enabled by default): OpenOnload userspace/runtime installation from vendor packages (optional; gracefully degrades if unavailable).

#### Mandatory Dependencies (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y liburing-dev build-essential pkg-config git python3
```

#### Optional Accelerator Dependencies (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y \
    libbpf-dev libxdp-dev libelf-dev zlib1g-dev clang llvm \
    libaio-dev libnuma-dev uuid-dev meson ninja-build libssl-dev
```

#### Build BoringSSL and lsquic (Required for TLS and QUIC)

Before building `kmx-aio` with TLS or QUIC support, run the helper script:

```bash
cd /path/to/kmx-aio
bash build/install_lsquic.sh
```

This clones and builds BoringSSL (required by all TLS streams and the QUIC engine)
and lsquic (required by the QUIC HTTP/3 features). Both are installed to `build/`
and linked into the library. **This step is mandatory** if you intend to use TLS
or QUIC; skip only if you disable both gates.

Verify optional dependencies if you plan to build with them enabled:

```bash
pkg-config --modversion liburing
pkg-config --modversion libbpf
pkg-config --modversion libxdp
pkg-config --modversion spdk_nvme
```

#### Install SPDK Libraries (Ubuntu/Debian)

SPDK is typically built from source to ensure all development libraries and
pkg-config metadata are available.

1. Install build dependencies:

```bash
sudo apt update
sudo apt install -y \
    build-essential pkg-config meson ninja-build git \
    python3 python3-jinja2 python3-pyelftools python3-tabulate \
    libaio-dev libnuma-dev uuid-dev libssl-dev libelf-dev libpcap-dev
```

   > **Note:** In current SPDK, `scripts/pkgdep/requirements.txt` is **not
   > committed** — it is generated at build time by `pip-compile`. Install
   > Python packages via `apt` as above instead of running `pip install -r`.

1. Clone and build SPDK:

```bash
cd /tmp
git clone https://github.com/spdk/spdk.git
cd spdk
git submodule update --init --recursive
./configure --with-shared
make -j"$(nproc)"
```

If you see generated RPC build errors like undefined
`rpc_trace_*_ctx` structures in `trace_rpc.c`, clean stale generated files and
rebuild:

```bash
git reset --hard && git clean -xfd
git submodule update --init --recursive
./configure --with-shared
make -j"$(nproc)"
```

If you see `implicit declaration of function 'OPENSSL_INIT_new'` or
`'OPENSSL_INIT_free'` errors, a **BoringSSL** installation at
`/usr/local/include/openssl/` is shadowing the system OpenSSL. GCC searches
`/usr/local/include` before `/usr/include` by default, so the BoringSSL
headers (which lack those functions) win.

The root fix is to move the orphaned BoringSSL files out of the way (they are
not tracked by any `.deb` package and nothing links against them at runtime):

```bash
sudo mv /usr/local/include/openssl /usr/local/include/openssl.boringssl.bak
sudo mv /usr/local/lib/libssl.a    /usr/local/lib/libssl.a.boringssl.bak
sudo mv /usr/local/lib/libcrypto.a /usr/local/lib/libcrypto.a.boringssl.bak
```

Then do a clean SPDK rebuild (no extra `--with-openssl` flag needed):

```bash
git reset --hard && git clean -xfd
git submodule update --init --recursive
./configure --with-shared --disable-werror
make -j"$(nproc)"
```

> **Note:** These stale BoringSSL files are left behind when a DPDK or SPDK
> dependency build is run with `sudo make install`. Only `sudo apt install` and
> `sudo make install` (for the final SPDK install step) should use elevated
> privileges. All `./configure` and `make` steps must run as a normal user.

If your SPDK checkout still fails to compile, try a stable release tag and
rebuild:

```bash
git fetch --tags
git checkout v24.09
git submodule update --init --recursive
./configure --with-shared --disable-werror
make -j"$(nproc)"
```

1. Install libraries and refresh linker cache:

```bash
sudo make install
sudo ldconfig
```

1. Verify expected libraries are available:

```bash
pkg-config --modversion spdk_nvme
pkg-config --modversion spdk_bdev
ldconfig -p | grep -E 'libspdk_(nvme|bdev|env_dpdk|util|log)'
```

1. If pkg-config still cannot find SPDK:

```bash
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
```

1. Runtime preparation for SPDK-backed I/O (hugepages):

```bash
echo 1024 | sudo tee /proc/sys/vm/nr_hugepages
```

To make hugepages persistent across reboot:

```bash
echo 'vm.nr_hugepages=1024' | sudo tee /etc/sysctl.d/99-spdk-hugepages.conf
sudo sysctl --system
```

1. Probe candidate SPDK bdev names with the discovery sample:

```bash
DISCOVERY_BIN=$(find ./source/default -path "*/kmx-aio-sample-spdk-discovery" -type f | head -1)
"$DISCOVERY_BIN"
"$DISCOVERY_BIN" Nvme0n1 Malloc0n1
```

Behavior:

```text
* no args: initialize SPDK subsystems and list currently registered bdev names
* args: validate only requested names that are already registered
```

If SPDK fails with `Permission denied` while opening files under
`/dev/hugepages` (for example `spdk_pid...map_0`), grant write access to your
user for the hugetlbfs mount:

```bash
sudo mkdir -p /dev/hugepages
sudo mount -t hugetlbfs nodev /dev/hugepages -o uid=$(id -u),gid=$(id -g),mode=1770,pagesize=2M
```

To make hugetlbfs mount permissions persistent across reboot, add to `/etc/fstab`:

```bash
nodev /dev/hugepages hugetlbfs uid=1000,gid=1000,mode=1770,pagesize=2M 0 0
```

Then mount from fstab without reboot:

```bash
sudo mount -a
```

Verify runtime state:

```bash
cat /proc/sys/vm/nr_hugepages
grep -i HugePages /proc/meminfo
ls -ld /dev/hugepages
mount | grep huge
```

One-shot apply (persistent + immediate):

```bash
echo 'vm.nr_hugepages=1024' | sudo tee /etc/sysctl.d/99-spdk-hugepages.conf
grep -q '^nodev /dev/hugepages hugetlbfs ' /etc/fstab || \
    echo 'nodev /dev/hugepages hugetlbfs uid=1000,gid=1000,mode=1770,pagesize=2M 0 0' | sudo tee -a /etc/fstab
sudo sysctl --system
sudo mkdir -p /dev/hugepages
sudo mount -a
```

## Optional Pillar 1 Feature Gates

Pillar 1 technologies are wired behind explicit QBS feature switches and are
**enabled by default**. Disable them at build time if your environment lacks the
required dependencies (e.g., SPDK libraries, libxdp, OpenOnload headers):

```bash
# Build without SPDK and AF_XDP, keep QUIC and AVB:
qbs build \
    projects.source.enable_spdk:false \
    projects.source.enable_af_xdp:false
```

Default state (all enabled):

* `projects.source.enable_openonload:true` — OpenOnload zero-copy accelerator (readiness only)
* `projects.source.enable_af_xdp:true` — AF_XDP packet socket (completion only)
* `projects.source.enable_spdk:true` — SPDK block device I/O (completion only)
* `projects.source.enable_quic:true` — QUIC engine and HTTP/3 (both models)
* `projects.source.enable_avb:true` — Audio Video Bridging (completion model only)

When enabled, compile-time defines are exported by `kmx-aio-lib`:

* `KMX_AIO_FEATURE_OPENONLOAD=1`
* `KMX_AIO_FEATURE_AF_XDP=1`
* `KMX_AIO_FEATURE_SPDK=1`
* `KMX_AIO_FEATURE_QUIC=1`
* `KMX_AIO_FEATURE_AVB=1`

## Architecture

The library is structured around root primitives and two execution models:

### Core Primitives

* **`kmx::aio::task<T>`**: A lazy-evaluation coroutine type. Tasks are the fundamental unit of asynchronous work.
* **`kmx::aio::executor_base`**: Shared lifecycle/synchronization base for readiness and completion executors.
* **`kmx::aio::allocator`**: Thread-local slab allocator for coroutine frame memory management.
* **`kmx::aio::buffer_pool` / `kmx::aio::buffer_handle`**: Fixed-capacity RAII buffer ownership for deterministic zero-copy patterns.
* **`kmx::aio::channel`**: SPSC queue with optional watermark/credit backpressure controls.
* **`kmx::aio::file_descriptor`**: RAII wrapper and syscall helpers for Linux file descriptors.
* **`kmx::aio::error_code`**: Type-safe error handling via `std::expected` and domain-specific error codes.

### Readiness-Model Namespace (`kmx::aio::readiness`)

Built on `epoll` for readiness-based I/O multiplexing. Call `co_await` when you need data; execution suspends until the kernel signals readiness.

| Component | Purpose |
| ----------- | ---------- |
| `executor` | Epoll-based executor; single-threaded or thread-pool |
| `executor_base` | Shared state and lifecycle |
| `tcp::listener`, `tcp::stream` | Async TCP accept/connect/read/write |
| `udp::socket` | Low-level async datagram I/O (recvmsg/sendmsg) |
| `udp::endpoint` | **High-level API**: span-based send/recv with automatic sockaddr management |
| `descriptor::epoll` | Low-level epoll descriptor wrapper and control |
| `descriptor::timer` | Timerfd wrapper; one-shot and periodic timers |
| `tls::stream` | Generic template over inner transport; BoringSSL Memory BIO-backed encrypted streams |
| `v4l2::capture` | **Zero-copy video capture**: mmap buffers auto-requeued on `frame_view` destruction |
| `quic::engine` | Template instantiation: `generic_engine<executor, udp::socket>` |
| `openonload::extensions` | Optional zero-copy acceleration via OpenOnload extensions (gracefully disabled if unavailable) |
| `http2::codec`, `http2::stream`, `http2::frame`, `http2::hpack` | HTTP/2 full serialization stack (model-agnostic) |

### Completion-Model Namespace (`kmx::aio::completion`)

Built on Linux `io_uring` for asynchronous completion-based I/O. Submit operations and `co_await` their completion; the kernel batches multiple ops and fires completions in bulk.

| Component | Purpose |
| ----------- | ---------- |
| `executor` | Io_uring-based executor; setup/teardown, completion polling, CQE batch draining |
| `executor::async_poll(fd, mask)` | One-shot `IORING_OP_POLL_ADD` for arbitrary fd readiness within io_uring (explicit re-arm per await) |
| `tcp::listener`, `tcp::stream` | Async TCP via io_uring |
| `udp::socket` | Async UDP via io_uring recvmsg/sendmsg; **no endpoint wrapper** (use socket directly) |
| `tls::stream` | Generic template; BoringSSL Memory BIO-backed |
| `timer` | Io_uring timeout operations |
| `v4l2::capture` | Hybrid V4L2 capture: io_uring poll (`POLLIN`) + synchronous `VIDIOC_DQBUF` |
| `quic::engine` | Template instantiation: `generic_engine<executor, udp::socket>` |
| `xdp::socket` | **AF_XDP kernel-bypass packet socket**: UMEM registration, RX/TX/fill/completion rings (gated: `enable_af_xdp`) |
| `spdk::runtime`, `spdk::device` | **SPDK NVMe/bdev access**: init, bdev enumeration, async device I/O (gated: `enable_spdk`) |
| `spdk::device` | Block device abstraction; includes malloc fallback backend |
| `avb::eth_socket` | Raw Ethernet socket with hardware timestamping |

### Cross-Model Abstractions

* **`kmx::aio::tls::stream<InnerStream>`**: Generic template; wraps TCP streams in both models; uses BoringSSL Memory BIOs for state machine decoupling from actual socket I/O.
* **`kmx::aio::quic::generic_engine<Executor, UdpSocket>`**: Template; lsquic backing; instantiated for both readiness and completion models.
* **`kmx::aio::http2::*`**: Codec, stream, frame, and HPACK serialization; no executor affinity; available to both models.
* **`kmx::aio::avb::*`**: IEEE 802.1 AVB/TSN stack (Completion model only); raw Ethernet socket, gPTP clock, SRP client.

### Memory and Synchronization

Readiness TCP/UDP/V4L2 classes are **move-only** and inherit `readiness::io_base` (copy-deleted, move-assign-deleted due to the non-reseatable `executor&` member).
Completion I/O classes follow similar move-only patterns to prevent accidental sharing of underlying file descriptors across executor contexts.

## Feature Availability Matrix

Quick reference showing which APIs are available in each execution model:

| Feature | Readiness (epoll) | Completion (io_uring) | Notes |
| --------- | ------ | ------ | ------- |
| **TCP** | ✅ Full | ✅ Full | Listener, stream, accept, read, write |
| **UDP Socket** | ✅ Full | ✅ Full | Low-level recvmsg/sendmsg |
| **UDP Endpoint** | ✅ High-level span API | ❌ Not available | Completion users manage `msghdr`/`iovec` directly |
| **TLS Stream** | ✅ Full | ✅ Full | Generic template; BoringSSL Memory BIO |
| **QUIC Engine** | ✅ Full (lsquic) | ✅ Full (lsquic) | HTTP/3 server/client; feature-gated |
| **Timers** | ✅ timerfd + epoll | ✅ io_uring timeout ops | Periodic and one-shot |
| **V4L2 Capture** | ✅ Zero-copy mmap + epoll | ✅ Hybrid poll + DQBUF | Completion uses `IORING_OP_POLL_ADD`, then `VIDIOC_DQBUF` |
| **HTTP/2** | ✅ Codec + ALPN | ✅ Codec + ALPN | No executor affinity |
| **AVB/IEEE 802.1** | ❌ Not yet implemented | ✅ Eth socket | Requires `CAP_NET_RAW` and hardware timestamps |
| **AF_XDP Packets** | ❌ Not available | ✅ Kernel-bypass (gated) | Feature-gated; eBPF packet filtering |
| **SPDK Block I/O** | ❌ Not available | ✅ NVMe/bdev (gated) | Feature-gated; DPDK-backed |
| **OpenOnload** | ✅ Zero-copy extensions | ❌ Not available | Readiness-only; headers-only; gracefully disabled |

**Legend:**

* ✅ Available / Fully implemented
* ❌ Not available in this model
* Feature-gated = Enabled by default; disable with `projects.source.enable_*:false`
* Headers-only = API declared but no implementation; graceful degradation at runtime

## Project Structure

```text
kmx-aio/
├── source/
│   ├── library/          # Core library source code
│   │   ├── api/kmx/aio/  # Public headers
│   │   │   ├── task.hpp, executor_base.hpp, file_descriptor.hpp, allocator.hpp, error_code.hpp
│   │   │   ├── readiness/           # epoll model APIs
│   │   │   │   ├── executor.hpp, tcp/, udp/, descriptor/, timer.hpp
│   │   │   │   ├── v4l2/            # V4L2 zero-copy capture
│   │   │   │   ├── tls/, quic/, openonload/, avb/
│   │   │   ├── completion/          # io_uring model APIs
│   │   │   │   ├── executor.hpp, tcp/, udp/, timer.hpp
│   │   │   │   ├── v4l2/, xdp/, spdk/, tls/, quic/, avb/
│   │   │   ├── http2/               # HTTP/2 codec, frames, HPACK
│   │   │   ├── avb/                 # Audio Video Bridging / IEEE 802.1
│   │   │   │   ├── eth_socket.hpp, gptp/, srp/
│   │   │   └── quic/                # QUIC generic engine
│   │   ├── inc/kmx/aio/             # Private headers
│   │   ├── src/                     # Implementation (.cpp) files
│   │   └── lib.qbs                  # Library build definition
│   ├── library-test/                # Unit tests and integration tests
│   │   └── unit-test.qbs
│   ├── sample/                      # Example applications
│   │   ├── readiness/               # Readiness model samples (epoll)
│   │   │   ├── tcp/                 # TCP echo, minimal server/client
│   │   │   ├── udp/                 # UDP echo, minimal server/client
│   │   │   ├── tls/                 # TLS echo, HTTP/2 ALPN examples
│   │   │   └── v4l2/                # V4L2 frame capture
│   │   └── completion/              # Completion model samples (io_uring)
│   │       ├── tcp/                 # TCP echo with io_uring
│   │       ├── udp/                 # UDP echo with io_uring
│   │       ├── tls/                 # TLS echo, HTTP/2 ALPN examples
│   │       ├── v4l2/                # V4L2 frame capture (io_uring poll hybrid)
│   │       ├── quic/                # QUIC echo server, HTTP/3 server/client
│   │       ├── spdk/                # SPDK bdev discovery, minimal block I/O
│   │       ├── xdp/                 # AF_XDP packet filter
│   │       └── hft/                 # High-frequency trading order router
│   └── source.qbs                   # Root build definition
├── build/
│   ├── install_lsquic.sh            # Build BoringSSL + lsquic
│   ├── boringssl/                   # BoringSSL repo (cloned by install_lsquic.sh)
│   └── lsquic/                      # lsquic repo (cloned by install_lsquic.sh)
└── README.md, LICENSE, etc.
```

## Usage Examples

### TCP Echo Server

```cpp
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/tcp/listener.hpp>
#include <kmx/aio/readiness/tcp/stream.hpp>
#include <iostream>

using namespace kmx::aio;

// Coroutine to handle a single client
task<void> handle_client(readiness::tcp::stream stream) {
    std::vector<char> buffer(1024);

    try {
        while (true) {
            // Asynchronously read data
            auto read_result = co_await stream.read(buffer);
            if (!read_result || *read_result == 0) break; // Error or EOF

            // Echo data back
            auto write_result = co_await stream.write(
                std::span(buffer.data(), *read_result)
            );
            if (!write_result) break;
        }
    } catch (...) {
        // Handle exceptions
    }
}

// Root task to accept connections
task<void> accept_loop(readiness::executor& exec) {
    readiness::tcp::listener listener(exec, "127.0.0.1", 8080);
    listener.listen();

    while (true) {
        auto accept_result = co_await listener.accept();
        if (accept_result) {
            readiness::tcp::stream client_stream(exec, std::move(*accept_result));
            exec.spawn(handle_client(std::move(client_stream)));
        }
    }
}

int main() {
    readiness::executor_config cfg{ .thread_count = 1 };
    readiness::executor exec(cfg);
    exec.spawn(accept_loop(exec));
    exec.run();
    return 0;
}
```

### UDP Echo Server (high-level API)

```cpp
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/udp/endpoint.hpp>
#include <netinet/in.h>
#include <cstring>

using namespace kmx::aio;

task<void> udp_echo(readiness::executor& exec) {
    auto ep = readiness::udp::endpoint::create(exec, AF_INET);
    if (!ep) co_return; // handle error

    // Bind to a port
    ::sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(9000u);
    addr.sin_addr.s_addr = INADDR_ANY;
    ::bind(ep->raw().get_fd(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    std::array<std::byte, 2048u> buf;
    while (true) {
        sockaddr_storage peer{};
        ::socklen_t peer_len{};

        auto recv_result = co_await ep->recv(buf, peer, peer_len);
        if (!recv_result) break;

        // Echo the datagram back to the sender
        co_await ep->send(
            std::span(buf.data(), *recv_result),
            reinterpret_cast<sockaddr*>(&peer), peer_len
        );
    }
}

int main() {
    readiness::executor_config cfg{ .thread_count = 1 };
    readiness::executor exec(cfg);
    exec.spawn(udp_echo(exec));
    exec.run();
    return 0;
}
```

### Async V4L2 Frame Capture (Readiness)

```cpp
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/v4l2/capture.hpp>

using namespace kmx::aio;

task<void> capture_frames(readiness::executor& exec) {
    auto cap = readiness::v4l2::capture::create(exec, {
        .device       = "/dev/video0",
        .format       = readiness::v4l2::fourcc::nv12,
        .size         = {1920u, 1080u},
        .fps          = {1u, 30u},  // 30 fps
        .buffer_count = 4u,
    });
    if (!cap) co_return; // handle error

    // Negotiated values may differ from requested
    auto& cfg = cap->config();

    while (true) {
        // Suspends via epoll until the driver has a filled buffer
        auto frame = co_await cap->next_frame();
        if (!frame) break; // device error

        // Zero-copy view into the mmap'd kernel buffer
        std::span<const std::byte> pixels = frame->data();
        const auto& meta = frame->metadata();

        process_frame(pixels, meta.width, meta.height, meta.timestamp_ns);

        // frame destructs here → VIDIOC_QBUF re-enqueues the buffer automatically
    }
}

int main() {
    readiness::executor_config cfg{ .thread_count = 1 };
    readiness::executor exec(cfg);
    exec.spawn(capture_frames(exec));
    exec.run();
    return 0;
}
```

The `capture::create()` factory performs the complete V4L2 initialisation sequence
(`QUERYCAP` → `S_FMT` → `S_PARM` → `REQBUFS` → `QUERYBUF`/`mmap` × N → `QBUF` × N →
`register_fd` → `STREAMON`) and returns a fully streaming device ready for
`next_frame()`. The negotiated pixel format, resolution and buffer count are
reflected back into `capture::config()` after construction.

No external library dependency is required — `linux/videodev2.h` is a standard
Linux kernel header.

### Async V4L2 Frame Capture (Completion Hybrid)

```cpp
#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/v4l2/capture.hpp>

using namespace kmx::aio;

task<void> capture_frames(std::shared_ptr<completion::executor> exec) {
    auto cap = completion::v4l2::capture::create(exec, {
        .device       = "/dev/video0",
        .format       = completion::v4l2::fourcc::yuyv,
        .size         = {1280u, 720u},
        .fps          = {1u, 30u},
        .buffer_count = 4u,
    });
    if (!cap) co_return;

    while (true) {
        // Suspends by submitting IORING_OP_POLL_ADD(POLLIN|POLLERR|...)
        auto frame = co_await cap->next_frame();
        if (!frame) break;

        process_frame(frame->data(), frame->metadata().width, frame->metadata().height,
                      frame->metadata().timestamp_ns);
        // frame destructs here -> VIDIOC_QBUF re-enqueues buffer
    }
}
```

Completion V4L2 is a hybrid model by kernel design: io_uring provides readiness
notification (`IORING_OP_POLL_ADD`), then the library performs `VIDIOC_DQBUF`
synchronously after wake-up. This keeps V4L2 capture in the same
`completion::executor` ring as sockets and timers.

The poll operation is intentionally one-shot: each `next_frame()` call re-submits
poll and then dequeues exactly one frame when the fd becomes ready.

### One-Shot Timer

```cpp
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/descriptor/timer.hpp>
#include <iostream>

using namespace kmx::aio;

task<void> delayed_action(readiness::executor& exec) {
    auto tmr = readiness::descriptor::timer::create(); // CLOCK_MONOTONIC, non-blocking
    if (!tmr) co_return;

    // Fire once after 500 ms
    itimerspec ts{};
    ts.it_value.tv_nsec = 500'000'000; // 500 ms
    tmr->set_time(0, ts);

    auto result = co_await tmr->wait(exec);
    if (result)
        std::cout << "Timer fired " << *result << " time(s)\n";
}

int main() {
    readiness::executor_config cfg{ .thread_count = 1 };
    readiness::executor exec(cfg);
    exec.spawn(delayed_action(exec));
    exec.run();
    return 0;
}
```

## Known Limitations and Model Differences

The library aims for API parity between readiness and completion models where architecturally possible, but explicit design decisions create intentional asymmetries:

### Model-Specific Constraints

**Completion Model (io_uring):**

* **UDP Endpoint Wrapper**: Not available. Completion API users operate directly on `completion::udp::socket` (low-level `recvmsg`/`sendmsg`). Readiness offers a higher-level `readiness::udp::endpoint` span-based API for convenience.
* **V4L2 Capture is Hybrid**: `completion::v4l2::capture` is implemented via one-shot `IORING_OP_POLL_ADD` + synchronous `VIDIOC_DQBUF`. Native io_uring submission ops for V4L2 ioctls are not available in current kernels, so this is the correct completion-model integration.
* **V4L2 MMAP Buffer Registration**: Device-backed mmap buffers cannot be passed to `io_uring_register_buffers()` (`EOPNOTSUPP`). Full camera-to-network registered-buffer zero-copy requires DMABUF/USERPTR pathways (not covered by this API).
* **`async_poll` is One-Shot**: `completion::executor::async_poll(fd, mask)` does not install a persistent subscription; long-running watchers must re-arm poll after each completion.

**Readiness Model (epoll):**

* **AF_XDP Packet Sockets**: Not available. Kernel-bypass packet I/O via AF_XDP is completion-only (`completion::xdp::socket`) as it requires careful polling loop integration with io_uring.
* **SPDK Block I/O**: Not available. NVMe and generic block device async I/O (`completion::spdk::*`) is completion-only to leverage io_uring's batch-completion advantages.

### Optional Feature Dependencies

**OpenOnload Integration (`readiness::openonload::extensions`)**:

* API is provided but is **headers-only** without a functional implementation.
* Symbol `KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE` (compile-time macro) is always `0` when the feature gate is disabled.
* If installed, the library can detect and wrap OpenOnload sockets at runtime; graceful degradation occurs if OpenOnload is unavailable.

**AVB / Audio Video Bridging (`completion::avb::*` only)**:

* Requires `CAP_NET_RAW` capability for raw Ethernet socket creation.
* Requires host NIC with IEEE 1588 PTP hardware timestamping support (most modern drivers support this; older NICs and virtual adapters may not).
* IEEE 802.1 SRP stream reservation operates only on links configured with 802.1Q VLAN support.

### Coroutine Frame Storage

All coroutine frames (`task<T>`) allocate from a thread-local slab allocator. Large frame objects (e.g., huge local `std::vector` or array) may cause allocator exhaustion. Structure your code to limit frame footprint or adjust slab parameters at executor initialization.

### Error Handling

The library uses `std::expected<T, error_code>` for error propagation. Uncaught exceptions in coroutine bodies are not automatically propagated; use explicit error handling.

### Platform Scope

* **Linux only**. No Windows, macOS, or other OS support.
* Requires **Linux kernel 5.10+** for stable io_uring; kernel 5.8+ may work but has known bugs.
* Requires **GCC 12+** or **Clang 15+** for C++26 coroutine features.

## Building

The project uses QBS for building.

```bash
qbs build profile:default
```

Or to build specifically the library or samples:

```bash
qbs build project:source # Builds everything in source/
```

## Static Analysis (clang-tidy)

Run clang-tidy across the project via the helper script in `source/`:

```bash
cd source
./clang-tidy.sh
```

The script:

* generates `compile_commands.json` using `qbs generate -g clangdb`
* runs `run-clang-tidy` with that compilation database

Optional environment variables:

* `PROFILE=<qbs-profile>` to force a specific QBS profile
* `BUILD_DIR=<dir>` to change the output directory (default: `default`)
* `JOBS=<n>` to control parallel clang-tidy workers
* `GCC_BIN=<path>` to choose which `g++` installation is used to derive the GCC toolchain path
* `GCC_TOOLCHAIN=<path>` to explicitly set the toolchain path passed to clang-tidy

Notes:

* The helper normalizes `-std=c++26` to `-std=c++2c` inside the generated compilation database for clang-tidy compatibility.
* The project build itself remains unchanged and still compiles as C++26 via QBS.

You can pass any extra `run-clang-tidy` arguments, for example:

```bash
cd source
./clang-tidy.sh -checks='-*,clang-analyzer-*,bugprone-*' -header-filter='^/home/io/Development/kmx-aio/source/library/'
```

## License

Copyright (C) 2026 - present KMX Systems. All rights reserved.
