# kmx-aio

**kmx-aio** is a modern, high-performance C++26 asynchronous I/O library designed for building non-blocking network applications on Linux. It leverages C++20 coroutines to provide a clean, synchronous-looking API for asynchronous operations across two execution models: readiness (`epoll`) and completion (`io_uring`).

## Key Features

*   **Modern C++26**: Built with the latest language standards.
*   **Coroutine-First Design**: Uses `co_await` for intuitive, sequential async code flow without callback hell.
*   **Readiness + Completion Models**: `epoll`-based readiness and `io_uring`-based completion APIs.
*   **Zero-Overhead Abstractions**: Lightweight wrappers around system calls.
*   **Type-Safe Error Handling**: Extensive use of `std::expected` and `std::error_code` for robust error management.
*   **TCP Networking**: Built-in support for TCP listeners and streams.
*   **UDP Networking**: Dual-layer UDP API in both models — low-level `readiness::udp::socket` / `completion::udp::socket` and high-level span/address APIs via their `udp::endpoint` wrappers.
*   **Async Timers**: Readiness timer (`timerfd` + `epoll`) and completion timer (`io_uring` timeout op).
*   **Async V4L2 Capture**: Zero-copy epoll-driven frame capture from any V4L2 streaming device (USB webcams, MIPI CSI-2 pipelines, GMSL camera chains). Frames land in `co_await`-returned `frame_view` objects that auto-requeue their mmap'd kernel buffers on destruction.

## Requirements

*   **Operating System**: Linux (requires `sys/epoll.h`).
*   **Compiler**: A C++ compiler supporting C++26 features (e.g., recent GCC or Clang).
*   **Build System**: QBS (Qt Build Suite).

## Dependencies

### Runtime / System Dependencies

*   **Linux kernel interfaces**: `epoll`, sockets, and `timerfd` (via headers such as `sys/epoll.h`, `sys/socket.h`, `sys/timerfd.h`).
*   **POSIX networking**: `arpa/inet.h`, `netinet/in.h`, and related socket APIs.

### Build Dependencies

*   **QBS**: used to configure and build all products (`qbs` CLI).
*   **C++26 toolchain**: compiler and standard library with support for coroutines and modern library features used by this project (for example `std::expected`, `std::span`, and `std::variant`).

### Test-Only Dependencies

*   **Catch2**: required only for `kmx-aio-test` (linked as `Catch2Main` and `Catch2` in `source/library-test/unit-test.qbs`).

### Third-Party Runtime Libraries

*   **None** beyond the standard C/C++ runtime and Linux system libraries.

### Optional Pillar 1 Prerequisites

These are only required when enabling the corresponding feature gate.

*   **AF_XDP** (`projects.source.enable_af_xdp:true`): `libbpf-dev`, `libxdp-dev`, `libelf-dev`, `zlib1g-dev`, `clang`, `llvm`.
*   **SPDK** (`projects.source.enable_spdk:true`): `libaio-dev`, `libnuma-dev`, `uuid-dev`, `meson`, `ninja-build`, and SPDK runtime libraries.
*   **OpenOnload** (`projects.source.enable_openonload:true`): OpenOnload userspace/runtime installation from vendor packages.

Ubuntu/Debian example:

```bash
sudo apt update
sudo apt install -y \
    libbpf-dev libxdp-dev libelf-dev zlib1g-dev clang llvm \
    libaio-dev libnuma-dev uuid-dev meson ninja-build
```

Quick verification:

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
- no args: initialize SPDK subsystems and list currently registered bdev names
- args: validate only requested names that are already registered
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
disabled by default:

*   `projects.source.enable_openonload:false`
*   `projects.source.enable_af_xdp:false`
*   `projects.source.enable_spdk:false`

Enable them at build time as needed:

```bash
qbs build \
    projects.source.enable_openonload:true \
    projects.source.enable_af_xdp:true \
    projects.source.enable_spdk:false
```

When enabled, compile-time defines are exported by `kmx-aio-lib`:

*   `KMX_AIO_FEATURE_OPENONLOAD=1`
*   `KMX_AIO_FEATURE_AF_XDP=1`
*   `KMX_AIO_FEATURE_SPDK=1`

## Architecture

The library is structured around root primitives and two execution models:

*   **`kmx::aio::task<T>`**: A lazy-evaluation coroutine type. Tasks are the fundamental unit of asynchronous work.
*   **`kmx::aio::executor_base`**: Shared lifecycle/synchronization base for readiness and completion executors.
*   **`kmx::aio::readiness::*`**: Readiness-model APIs (`epoll`) for TCP/UDP/TLS operations.
*   **`kmx::aio::completion::*`**: Completion-model APIs (`io_uring`) for TCP/UDP/TLS operations.
*   **`kmx::aio::readiness::executor`**: epoll-based executor for readiness APIs.
*   **`kmx::aio::completion::executor`**: io_uring-based executor for completion APIs.
*   **`kmx::aio::readiness::io_base`** and **`kmx::aio::completion::io_base`**: Model-specific shared I/O bases used by TCP/UDP wrappers.
*   **`kmx::aio::readiness::tcp::*` / `kmx::aio::completion::tcp::*`**: Asynchronous TCP listener/stream APIs for each execution model.
*   **`kmx::aio::readiness::udp::*` / `kmx::aio::completion::udp::*`**: Low-level socket + high-level endpoint UDP APIs for each execution model.
*   **`kmx::aio::readiness::descriptor::timer`** and **`kmx::aio::completion::timer`**: Timer APIs for the two execution models.
*   **`kmx::aio::readiness::v4l2::capture`**: Async V4L2 video capture. MMAP streaming buffers managed by the kernel; a single `co_await next_frame()` suspends via epoll until a filled buffer is ready. The returned `frame_view` is a zero-copy view into the mmap'd buffer that automatically re-enqueues the buffer (VIDIOC_QBUF) on destruction. Supports any V4L2 streaming device: USB webcams, MIPI CSI-2 ISPs, GMSL serialiser/deserialiser pipelines, DVB capture cards.

Readiness TCP/UDP/V4L2 classes are move-only and inherit `readiness::io_base` (copy-deleted, move-assign-deleted due to the non-reseatable `executor&` member).

## Project Structure

```
kmx-aio/
├── source/
│   ├── library/          # Core library source code
│   │   ├── inc/kmx/aio/  # Public headers
│   │   │   ├── executor_base.hpp    # Shared base for executor state/lifetime controls
│   │   │   ├── file_descriptor.hpp  # RAII file-descriptor wrapper and syscall helpers
│   │   │   ├── task.hpp             # Lazy coroutine task<T> type
│   │   │   ├── readiness/           # epoll model APIs (tcp/udp/tls/v4l2 + descriptor)
│   │   │   │   └── v4l2/            # V4L2 capture: v4l2_types.hpp, capture.hpp
│   │   │   ├── completion/          # io_uring model APIs (tcp/udp/tls/xdp)
│   │   └── src/                     # Implementation (.cpp) files
│   ├── sample/           # Example applications
│   │   ├── readiness/    # Readiness-model samples (epoll)
│   │   │   ├── tcp/
│   │   │   │   ├── minimal/
│   │   │   │   └── echo/
│   │   │   ├── udp/
│   │   │   │   ├── minimal/
│   │   │   │   └── echo/
│   │   │   ├── tls/
│   │   │   │   └── echo_readiness_server/
│   │   │   └── v4l2/
│   │   │       └── capture/         # sample-v4l2-capture
│   │   └── completion/   # Completion-model samples (io_uring)
│   │       ├── tcp/
│   │       │   └── echo_uring/
│   │       ├── udp/
│   │       │   └── echo_uring/
│   │       └── tls/
│   │           └── echo_completion_server/
│   └── library-test/     # Unit tests
└── build/                # Build artifacts
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
    addr.sin_port        = htons(9000);
    addr.sin_addr.s_addr = INADDR_ANY;
    ::bind(ep->raw().get_fd(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    std::array<std::byte, 2048> buf;
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

### Async V4L2 Frame Capture

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
