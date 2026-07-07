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
* **QUIC + [HTTP/3](https://www.rfc-editor.org/rfc/rfc9114)**: Full QUIC engine (both models) with [lsquic](https://github.com/litespeedtech/lsquic) backing; HTTP/3 server/client samples included.
* **Async Timers**: Readiness timer (`timerfd` + `epoll`) and completion timer (`io_uring` timeout op).
* **[V4L2](https://linuxtv.org) Async Capture** (Readiness + Completion): Readiness mode uses epoll-driven frame capture; completion mode uses `IORING_OP_POLL_ADD` plus synchronous `VIDIOC_DQBUF` (hybrid model) in the same io_uring executor. Targets V4L2 streaming devices such as USB webcams, MIPI CSI-2 pipelines, and GMSL camera chains. Frames land in `co_await`-returned `frame_view` objects that auto-requeue mmap'd kernel buffers on destruction.
* **Completion `async_poll(fd, mask)`**: First-class one-shot `IORING_OP_POLL_ADD` primitive to await arbitrary fd readiness (V4L2, eventfd, timerfd, signalfd, netlink) inside `completion::executor`; callers re-arm by invoking it again.
* **Buffer Pool Primitives**: `kmx::aio::buffer_pool` and `kmx::aio::buffer_handle` provide fixed-capacity RAII buffer leasing for deterministic zero-copy workflows.
* **Channel Backpressure**: `kmx::aio::channel` supports watermark-based producer throttling and credit reporting.
* **HTTP/2**: Full codec, stream, frame, and HPACK serialization stack (no model affinity).
* **OPC UA** (feature-gated): Async client/server/subscription facade with [open62541](https://open62541.org) backend support, plus shim fallback for feature-off builds and tests.
* **AVB (Audio Video Bridging, [IEEE 802.1](https://1.ieee802.org/avbridges/))** (Completion model): Raw Ethernet socket with hardware timestamping, gPTP clock synchronization, and SRP client for stream reservation.
* **[AF_XDP Packet Socket](https://www.kernel.org/doc/html/latest/networking/af_xdp.html)** (Completion model, gated): Kernel-bypass packet filtering with eBPF support and UMEM ring management.
* **[SPDK Block I/O](https://spdk.io/)** (Completion model, gated): NVMe, generic bdev, and storage acceleration via DPDK.

## Requirements

* **Operating System**: Linux (requires `sys/epoll.h`).
* **Compiler**: GCC or Clang with C++26 support; standard library must provide `std::expected`, `std::span`, `std::variant`, and coroutines (GCC 12+ or Clang 15+).
* **Build Tool**: [QBS (Qt Build Suite)](https://wiki.qt.io/Qbs) — configure and build all products via the `qbs` CLI.

## Dependencies

### Runtime / System Dependencies

* **Linux kernel interfaces**: `epoll`, sockets, `timerfd`, and `io_uring` (via `liburing`).
* **POSIX networking**: `arpa/inet.h`, `netinet/in.h`, and related socket APIs.
* **[liburing](https://github.com/axboe/liburing)** (required for completion model): `liburing-dev` package provides headers and runtime for io_uring async I/O.

### Test-Only Dependencies

* **[Catch2](https://github.com/catchorg/Catch2)**: required only for `kmx-aio-test` (linked as `Catch2Main` and `Catch2` in `source/library-test/unit-test.qbs`).

### Third-Party Runtime Libraries

* **None** required by default beyond the standard C/C++ runtime and Linux system libraries.
* When `project.enable_opc_ua:true`, [open62541](https://open62541.org) is linked as a static archive from the local vendored prefix.

### Optional Feature-Gated Prerequisites

These are only required when the corresponding feature gate is enabled.
Most gates are on by default; disable any gate if you lack its dependency.

* **QUIC / HTTP/3** (enabled by default): [BoringSSL](https://boringssl.googlesource.com/boringssl/) and [lsquic](https://github.com/litespeedtech/lsquic) libraries. Build both via `build/install_lsquic.sh` (see below).
* **OPC UA** (disabled by default): [open62541](https://open62541.org) headers and library. Build locally via `build/install_open62541.sh` (see below), then enable with QBS feature flags.
* **AF_XDP** (enabled by default): `libbpf-dev`, `libxdp-dev`, `libelf-dev`, `zlib1g-dev`, `clang`, `llvm` ([libbpf](https://github.com/libbpf/libbpf), [libxdp](https://github.com/xdp-project/xdp-tools)).
* **SPDK** (enabled by default): `libaio-dev`, `libnuma-dev`, `uuid-dev`, `meson`, `ninja-build`, and [SPDK](https://spdk.io) runtime libraries.
* **AVB / [IEEE 802.1](https://1.ieee802.org/avbridges/)** (Completion model, enabled by default): Kernel with PTP/hardware timestamping support (most modern NIC drivers); user must have `CAP_NET_RAW` privilege.
* **OpenOnload** (enabled by default): [OpenOnload](https://github.com/Xilinx/onload) userspace/runtime installation from vendor packages (optional; gracefully degrades if unavailable).

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

Before building `kmx-aio` with TLS or QUIC support, run the helper script from the repository root:

```bash
bash build/install_lsquic.sh
```

This clones and builds [BoringSSL](https://boringssl.googlesource.com/boringssl/) (required by all TLS streams and the QUIC engine)
and [lsquic](https://github.com/litespeedtech/lsquic) (required by the QUIC HTTP/3 features). Both are installed to `build/`
and linked into the library. **This step is mandatory** if you intend to use TLS
or QUIC; skip only if you disable both gates.

Verify optional dependencies if you plan to build with them enabled:

```bash
pkg-config --modversion liburing
pkg-config --modversion libbpf
pkg-config --modversion libxdp
pkg-config --modversion spdk_nvme
```

#### Build open62541 for OPC UA (Optional, Feature-Gated)

Build and install [open62541](https://open62541.org) into the local workspace prefix by running the helper script from the repository root:

```bash
bash build/install_open62541.sh
```

Enable OPC UA with vendored static linkage:

```bash
qbs build -f source/source.qbs config:debug -j"$(nproc)" \
    project.enable_opc_ua:true \
    project.opc_ua_vendored:true \
    project.opc_ua_prefix:"$PWD/build/open62541/install-local"
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
DISCOVERY_BIN=$(find ./debug ./default -path "*/kmx-aio-sample-spdk-discovery" -type f 2>/dev/null | head -1)
[ -n "$DISCOVERY_BIN" ] || { echo "kmx-aio-sample-spdk-discovery not found"; exit 1; }
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

## Optional Feature Gates

These feature-gated subsystems are wired behind explicit QBS feature switches and are
**mostly enabled by default** (OPC UA is currently default-off). Disable them at build time if your environment lacks the
required dependencies (e.g., SPDK libraries, libxdp, OpenOnload headers):

```bash
# Build without SPDK and AF_XDP, keep QUIC and AVB:
qbs build -f source/source.qbs \
    project.enable_spdk:false \
    project.enable_af_xdp:false
```

Default state (`source/source.qbs`):

* `project.enable_openonload:true` — OpenOnload zero-copy accelerator (readiness only)
* `project.enable_af_xdp:true` — AF_XDP packet socket (completion only)
* `project.enable_spdk:true` — SPDK block device I/O (completion only)
* `project.enable_quic:true` — QUIC engine and HTTP/3 (both models)
* `project.enable_avb:true` — Audio Video Bridging (completion model only)
* `project.enable_opc_ua:false` — OPC UA facade/backend integration (off by default)

When enabled, compile-time defines are exported by `kmx-aio-lib`:

* `KMX_AIO_FEATURE_OPENONLOAD=1`
* `KMX_AIO_FEATURE_AF_XDP=1`
* `KMX_AIO_FEATURE_SPDK=1`
* `KMX_AIO_FEATURE_QUIC=1`
* `KMX_AIO_FEATURE_AVB=1`
* `KMX_AIO_FEATURE_OPC_UA=1` (only when `project.enable_opc_ua:true`)

## OPC UA Support

Based on the [OPC UA specification](https://opcfoundation.org) with [open62541](https://open62541.org) as the feature-on backend.

### Current Scope

* Async OPC UA facade APIs: `kmx::aio::opc_ua::client`, `kmx::aio::opc_ua::server`, and `kmx::aio::opc_ua::subscription`.
* Open62541 compatibility boundary: `source/library/inc/kmx/aio/opc_ua/open62541_compat.hpp`.
* Feature-off shim path for deterministic tests and portability.

### Behavior Notes

* Method call outputs are surfaced as strings in `method_call_result.output_arguments`.
* Scalar conversions include string, signed/unsigned integers, boolean, float, and double.
* Float/double string formatting currently follows compact `std::to_string`-normalized output in integration tests (for example `"1.25"`, `"2.5"`).
* Unsupported output types map to `UA_STATUSCODE_BADTYPEMISMATCH` through the async callback bridge.

### OPC UA Test Commands

Run fast OPC UA client service tests (exclude slow integration cases):

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "[opc_ua][client][service]~[slow]"
```

Run slow integration conversion cases:

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "opc_ua compat async call converts typed outputs"
"$TEST_BIN" "opc_ua compat async call returns bad type mismatch for unsupported output"
```

## Testing Workflow

Run the local equivalent of `.github/workflows/ci-avb.yml` from the versioned canonical script:

```bash
bash scripts/ci/run-ci-avb-local.sh --only all
```

Run a single CI-equivalent job locally:

```bash
bash scripts/ci/run-ci-avb-local.sh --only build-and-test
bash scripts/ci/run-ci-avb-local.sh --only quic-smoke
bash scripts/ci/run-ci-avb-local.sh --only gpu-smoke
```

The canonical script lives in `scripts/ci/` (versioned). On each run it sync-copies itself to
`build/run-ci-avb-local.sh` for local convenience.

Build and run tests with a timeout so local runs cannot block indefinitely:

```bash
qbs build -f source/source.qbs config:debug -j"$(nproc)"
bash build/run-tests-bounded.sh
```

Run the previously flaky channel wait case repeatedly (local flake guard):

```bash
RUNS=40 TIMEOUT_SECONDS=20 TEST_FILTER="channel wait_until_can_send unblocks when consumer pops from a full ring" \
    bash build/run-tests-bounded.sh
```

Run sanitizer builds/tests:

```bash
bash build/run-sanitizer-tests.sh
```

Run QUIC smoke tests explicitly (readiness + completion):

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "[quic][readiness][integration][smoke]"
"$TEST_BIN" "[quic][http3][integration][smoke]"
```

Optional QUIC smoke tuning environment variables:

* `KMX_AIO_QUIC_READINESS_WATCHDOG_NS` (default `10000000`): readiness QUIC watchdog tick in nanoseconds.
    Accepted range is `1000000` to `100000000` (1 ms to 100 ms). Invalid values fall back to default.
* `KMX_QUIC_ECHO_PORT`: override readiness QUIC echo sample port (default `12345`).
* `KMX_QUIC_HTTP3_PORT`: override completion QUIC HTTP/3 sample port (default `12345`).

These are useful when running parallel smoke tests to avoid fixed-port collisions.

Run GPU smoke locally (requires NVIDIA driver + CUDA runtime/toolkit):

```bash
qbs build --products sample-gpu-image-processing,kmx-aio-test -f source/source.qbs config:debug -j"$(nproc)" \
    project.enable_openonload:false \
    project.enable_af_xdp:false \
    project.enable_spdk:false \
    project.enable_quic:false \
    project.enable_cuda:true

SAMPLE_BIN="$(find debug -type f -name sample-gpu-image-processing | head -n 1)"
LD_LIBRARY_PATH=/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-} \
    "$SAMPLE_BIN" --max-frames 1 --width 320 --height 240 --buffer-count 2 --gpu-device 0

TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
LD_LIBRARY_PATH=/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-} "$TEST_BIN" "[gpu]"
```

Run GPU smoke with NVIDIA forced on Linux PRIME on-demand profiles:

```bash
__NV_PRIME_RENDER_OFFLOAD=1 \
__GLX_VENDOR_LIBRARY_NAME=nvidia \
__VK_LAYER_NV_optimus=NVIDIA_only \
LD_LIBRARY_PATH=/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-} \
    bash scripts/ci/run-ci-avb-local.sh --only gpu-smoke
```

Optional quick checks before running forced-NVIDIA smoke:

```bash
nvidia-smi
ls /usr/include/cuda_runtime.h /usr/local/cuda/include/cuda_runtime.h 2>/dev/null
```

GPU smoke troubleshooting (local):

* `nvidia-smi: command not found` or no GPUs listed:
    NVIDIA driver/runtime is missing or not active on this machine.
* `cuda_runtime.h: No such file or directory` during build:
    CUDA toolkit headers are missing; install CUDA toolkit and retry.
* `GLIBCXX_3.4.35 not found` at runtime:
    Run binaries with `LD_LIBRARY_PATH=/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}`.
* V4L2 camera unavailable (`/dev/video0` open/configure failure):
    Sample falls back to synthetic frames; use `--device` to point to another V4L2 node.

Sanitizer feature toggles are exposed via QBS project properties:

* `project.enable_asan:true`
* `project.enable_tsan:true`

Use one sanitizer at a time.

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
| `spdk::runtime`, `spdk::device` | **SPDK NVMe/bdev access**: init, bdev enumeration, async device I/O; `spdk::device` includes malloc fallback backend (gated: `enable_spdk`) |
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
| **OPC UA** | ✅ (feature-gated) | ✅ (feature-gated) | Backend-neutral facade; not executor-model-specific; disabled by default |

**Legend:**

* ✅ Available / Fully implemented
* ❌ Not available in this model
* ⚙ Feature-gated — gate-controlled via `project.enable_*`; most are on by default; `enable_opc_ua` is off by default
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
│   │   │   ├── opc_ua/              # OPC UA facade (feature-gated)
│   │   │   │   └── client.hpp, server.hpp, subscription.hpp, types.hpp, error.hpp
│   │   │   └── quic/                # QUIC generic engine
│   │   ├── inc/kmx/aio/             # Private headers (opc_ua/open62541_compat.hpp, ...)
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
│   ├── install_open62541.sh         # Build open62541 for OPC UA
│   ├── boringssl/                   # BoringSSL repo (cloned by install_lsquic.sh)
│   ├── lsquic/                      # lsquic repo (cloned by install_lsquic.sh)
│   └── open62541/                   # open62541 repo (cloned by install_open62541.sh)
└── README.md, LICENSE, etc.
```

## Usage Examples

### TCP Echo Server

```cpp
#include <iostream>
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/tcp/listener.hpp>
#include <kmx/aio/readiness/tcp/stream.hpp>
#include <span>
#include <utility>
#include <vector>

using namespace kmx::aio;

// Coroutine to handle a single client
task<void> handle_client(readiness::tcp::stream stream)
{
    std::vector<char> buffer(1024u);

    try
    {
        while (true)
        {
            // Asynchronously read data
            auto read_result = co_await stream.read(buffer);
            if (!read_result || *read_result == 0)
                break; // Error or EOF

            // Echo data back
            auto write_result = co_await stream.write(std::span(buffer.data(), *read_result));
            if (!write_result)
                break;
        }
    }
    catch (...)
    {
        // Handle exceptions
    }
}

// Root task to accept connections
task<void> accept_loop(readiness::executor& exec)
{
    readiness::tcp::listener listener(exec, "127.0.0.1", 8080u);
    listener.listen();

    while (true)
    {
        auto accept_result = co_await listener.accept();
        if (accept_result)
        {
            readiness::tcp::stream client_stream(exec, std::move(*accept_result));
            exec.spawn(handle_client(std::move(client_stream)));
        }
    }
}

int main()
{
    readiness::executor_config cfg{ .thread_count = 1u };
    readiness::executor exec(cfg);
    exec.spawn(accept_loop(exec));
    exec.run();
    return 0;
}
```

### UDP Echo Server (high-level API)

```cpp
#include <array>
#include <arpa/inet.h>
#include <cstddef>
#include <cstring>
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/udp/endpoint.hpp>
#include <netinet/in.h>
#include <span>
#include <sys/socket.h>

using namespace kmx::aio;

task<void> udp_echo(readiness::executor& exec)
{
    auto ep = readiness::udp::endpoint::create(exec, AF_INET);
    if (!ep)
        co_return; // handle error

    // Bind to a port
    ::sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(9000u);
    addr.sin_addr.s_addr = INADDR_ANY;
    ::bind(ep->raw().get_fd(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    std::array<std::byte, 2048u> buf;
    while (true)
    {
        sockaddr_storage peer{};
        ::socklen_t peer_len{};

        auto recv_result = co_await ep->recv(buf, peer, peer_len);
        if (!recv_result)
            break;

        // Echo the datagram back to the sender
        co_await ep->send(std::span(buf.data(), *recv_result), reinterpret_cast<sockaddr*>(&peer), peer_len);
    }
}

int main()
{
    readiness::executor_config cfg{ .thread_count = 1u };
    readiness::executor exec(cfg);
    exec.spawn(udp_echo(exec));
    exec.run();
    return 0;
}
```

### Async V4L2 Frame Capture (Readiness)

```cpp
#include <cstddef>
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/v4l2/capture.hpp>
#include <span>

using namespace kmx::aio;

task<void> capture_frames(readiness::executor& exec)
{
    auto cap = readiness::v4l2::capture::create(exec,
                                                {
                                                    .device       = "/dev/video0",
                                                    .format       = readiness::v4l2::fourcc::nv12,
                                                    .size         = {1920u, 1080u},
                                                    .fps          = {1u, 30u}, // 30 fps
                                                    .buffer_count = 4u,
                                                });
    if (!cap)
        co_return; // handle error

    // Negotiated values may differ from requested
    auto& cfg = cap->config();

    while (true)
    {
        // Suspends via epoll until the driver has a filled buffer
        auto frame = co_await cap->next_frame();
        if (!frame)
            break; // device error

        // Zero-copy view into the mmap'd kernel buffer
        std::span<const std::byte> pixels = frame->data();
        const auto& meta = frame->metadata();

        process_frame(pixels, meta.width, meta.height, meta.timestamp_ns);

        // frame destructs here → VIDIOC_QBUF re-enqueues the buffer automatically
    }
}

int main()
{
    readiness::executor_config cfg{ .thread_count = 1u };
    readiness::executor exec(cfg);
    exec.spawn(capture_frames(exec));
    exec.run();
    return 0;
}
```

The `capture::create()` factory performs the complete V4L2 initialisation sequence
(`QUERYCAP` → `S_FMT` → `S_PARM` → `REQBUFS` → `QUERYBUF`/`mmap` × N → `QBUF` × N →
`register_fd` → `STREAMON`) and returns a fully streaming device ready for
`next_frame()`, targeting common streaming endpoints including USB webcams,
MIPI CSI-2 camera pipelines, and GMSL camera chains. The negotiated pixel
format, resolution and buffer count are
reflected back into `capture::config()` after construction.

No external library dependency is required — `linux/videodev2.h` is a standard
Linux kernel header. See the [Video4Linux community hub](https://linuxtv.org) for
the V4L2 API specification and `v4l2-compliance` test suite.

### Async V4L2 Frame Capture (Completion Hybrid)

```cpp
#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/v4l2/capture.hpp>
#include <memory>

using namespace kmx::aio;

task<void> capture_frames(std::shared_ptr<completion::executor> exec)
{
    auto cap = completion::v4l2::capture::create(exec,
                                                 {
                                                     .device       = "/dev/video0",
                                                     .format       = completion::v4l2::fourcc::yuyv,
                                                     .size         = {1280u, 720u},
                                                     .fps          = {1u, 30u},
                                                     .buffer_count = 4u,
                                                 });
    if (!cap)
        co_return;

    while (true)
    {
        // Suspends by submitting IORING_OP_POLL_ADD(POLLIN|POLLERR|...)
        auto frame = co_await cap->next_frame();
        if (!frame)
            break;

        process_frame(
            frame->data(), frame->metadata().width, frame->metadata().height, frame->metadata().timestamp_ns
        );
        // frame destructs here -> VIDIOC_QBUF re-enqueues buffer
    }
}
```

Completion V4L2 is a hybrid model by kernel design: io_uring provides readiness
notification (`IORING_OP_POLL_ADD`), then the library performs `VIDIOC_DQBUF`
synchronously after wake-up. This keeps V4L2 capture in the same
`completion::executor` ring as sockets and timers.
It targets the same streaming device classes as readiness mode, including USB
webcams, MIPI CSI-2 camera pipelines, and GMSL camera chains.

The poll operation is intentionally one-shot: each `next_frame()` call re-submits
poll and then dequeues exactly one frame when the fd becomes ready.

### One-Shot Timer

```cpp
#include <iostream>
#include <kmx/aio/readiness/descriptor/timer.hpp>
#include <kmx/aio/readiness/executor.hpp>
#include <time.h>

using namespace kmx::aio;

task<void> delayed_action(readiness::executor& exec)
{
    auto tmr = readiness::descriptor::timer::create(); // CLOCK_MONOTONIC, non-blocking
    if (!tmr)
        co_return;

    // Fire once after 500 ms
    itimerspec ts{};
    ts.it_value.tv_nsec = 500'000'000u; // 500 ms
    tmr->set_time(0, ts);

    auto result = co_await tmr->wait(exec);
    if (result)
        std::cout << "Timer fired " << *result << " time(s)\n";
}

int main()
{
    readiness::executor_config cfg{ .thread_count = 1u };
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

### AVB Two-Host Quick Start (gPTP + SRP + AVTP)

Use this flow to run the completion AVB talker/listener samples with matching stream identity and SRP reservation.

1. Build the project:

```bash
# Run from the repository root
qbs build -f source/source.qbs config:debug -j"$(nproc)"
```

1. Locate the sample binaries (hashed build directories):

```bash
find debug -type f -name sample-avb-talker
find debug -type f -name sample-avb-listener
```

1. Ensure runtime capabilities (raw Ethernet + clock discipline):

```bash
sudo setcap cap_net_raw,cap_sys_time+ep /path/to/sample-avb-talker
sudo setcap cap_net_raw,cap_sys_time+ep /path/to/sample-avb-listener
```

1. Start talker on Host A:

```bash
/path/to/sample-avb-talker \
    --iface eth0 \
    --dest-mac 91:E0:F0:00:0E:80 \
    --stream-id 1 \
    --sync-timeout-s 5 \
    --period-us 125
```

1. Start listener on Host B (must subscribe to the exact talker stream id + source MAC):

```bash
/path/to/sample-avb-listener \
    --iface eth0 \
    --talker-mac 02:11:22:33:44:55 \
    --stream-id 1 \
    --sync-timeout-s 5 \
    --period-us 125
```

1. Verify expected behavior in logs:

* Talker prints `synced=true` and ongoing `offset/path_delay` diagnostics.
* Listener prints `synced=true`, increasing `parsed`, and jitter statistics.
* SRP failures appear early as explicit `SRP start/advertise/subscribe failed` errors.

Optional diagnostics-only bring-up (no AVTP payload send/receive loop):

```bash
/path/to/sample-avb-talker --iface eth0 --dest-mac 91:E0:F0:00:0E:80 --stream-id 1 --diagnostics-only
/path/to/sample-avb-listener --iface eth0 --talker-mac 02:11:22:33:44:55 --stream-id 1 --diagnostics-only
```

Notes:

* `--talker-mac` is the talker source NIC MAC address, not the AVTP multicast destination.
* The gPTP clock is now a startup gate; if there is no reachable gPTP grandmaster on the segment, both samples will timeout before AVTP streaming.

### Single-Host Lab Mode (Limited Validation)

When you do not have a second host, gPTP grandmaster, or AVB-capable switch, you can still validate build/CLI behavior and failure paths.

1. Basic CLI/argument validation:

```bash
debug/sample-avb-talker.*/sample-avb-talker --help | head -n 20
debug/sample-avb-listener.*/sample-avb-listener --help | head -n 20
```

1. MAC parser validation (expected: non-zero exit + explicit invalid MAC error):

```bash
debug/sample-avb-listener.*/sample-avb-listener --iface eth0 --talker-mac not-a-mac --stream-id 1
```

1. gPTP/SRP timeout-path validation (expected: startup timeout error without AVB fabric):

```bash
debug/sample-avb-talker.*/sample-avb-talker \
    --iface eth0 \
    --dest-mac 91:E0:F0:00:0E:80 \
    --stream-id 1 \
    --sync-timeout-s 2 \
    --max-frames 1
```

This mode is useful for regression checks in development, but it does not validate end-to-end synchronized AVTP delivery. Use the two-host flow above for functional AVB verification.

### AVB Troubleshooting

| Symptom | Likely Cause | Action |
| --- | --- | --- |
| `gPTP sync failed` / startup timeout | No reachable grandmaster or PTP disabled on NIC/switch | Verify PTP domain and gm presence; increase `--sync-timeout-s` for slow convergence |
| `SRP advertise failed` or `SRP subscribe failed` | VLAN/SRP path not configured or blocked | Confirm 802.1Q VLAN and SRP support end-to-end on the segment |
| Listener `parsed=0` while `rx` increases | `--talker-mac` / `--stream-id` mismatch | Match listener arguments to the active talker source MAC and stream id |
| Persistent high jitter | Clock not stably synchronized or network contention | Check `synced`, `offset`, `path_delay`; validate traffic class/QoS and link health |

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

Dependency-light baseline build (works without optional accelerator stacks such as
SPDK/AF_XDP/OpenOnload/QUIC):

```bash
qbs resolve -f source/source.qbs config:debug \
    project.enable_openonload:false \
    project.enable_af_xdp:false \
    project.enable_spdk:false \
    project.enable_quic:false
qbs build   -f source/source.qbs config:debug \
    project.enable_openonload:false \
    project.enable_af_xdp:false \
    project.enable_spdk:false \
    project.enable_quic:false
```

Full-feature build (requires all optional dependencies installed):

```bash
qbs resolve -f source/source.qbs config:debug
qbs build   -f source/source.qbs config:debug # Builds everything in source/
```

If QBS reports a profile/configuration mismatch (for example, "profile 'none' was
used when last building"), run `qbs resolve` first for the same `-f`, `profile`,
and `config` values you intend to build with.

If you want to force a specific profile, use `profile:<name>` only after that
profile exists in your local QBS setup.

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
./clang-tidy.sh -checks='-*,clang-analyzer-*,bugprone-*' -header-filter='^.*/source/library/'
```

## License

Copyright (C) 2026 - present KMX Systems. All rights reserved.
