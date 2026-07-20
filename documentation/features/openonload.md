# OpenOnload

[OpenOnload](https://github.com/Xilinx/openonload) is a kernel-bypass network acceleration library from AMD/Xilinx that transparently intercepts POSIX socket calls and redirects I/O through hardware NIC queues via an `LD_PRELOAD` shim. It eliminates kernel-mode context switches for network data paths on supported NICs (Solarflare/Xilinx adapters), typically halving latency and significantly increasing throughput for latency-critical workloads.

In this repository, OpenOnload integration is **feature-gated and readiness-only**. The entire implementation is headers-only (`extensions.hpp`); no library linking is required beyond what the `LD_PRELOAD` shim provides. All functions compile cleanly without OpenOnload headers installed, with graceful no-op fallback.

## Scope

- Executor backend selection at construction (`epoll_only`, `openonload_preferred`, `openonload_required`)
- Runtime detection via environment variables without link-time dependency
- Zero-copy receive path for `readiness::tcp::stream::read()` via `onload_zc_recv`
- Zero-copy send via `onload_zc_alloc_buffers` + `onload_zc_send`
- Accelerated fd detection via `onload_fd_stat`
- Graceful degradation: falls back to standard `epoll` + POSIX `read`/`write` when unavailable

## Architecture

```
readiness::executor construction
         │
         ▼
 is_openonload_runtime_available()
 ┌──────────────────────────────────────────────┐
 │  checks env vars at runtime (no link needed) │
 │  LD_PRELOAD containing "onload"              │
 │  ONLOAD_STACKNAME set                        │
 │  EF_POLL_USEC set                            │
 └──────────────────────────────────────────────┘
         │
    available?
    ┌────┴─────┐
   YES        NO
    │          │
openonload_   epoll
preferred  ──►only
    │
    ▼
openonload::initialize_runtime_stack("kmxaio_fast_stack")
    │
    ▼
tcp::stream::read()
    ├── is_accelerated_fd(fd)?  ──► onload_zc_recv  →  ZC copy to std::span → co_return
    │                               (fallback to ::read on ENOTSUP)
    └── not accelerated         ──► ::read (standard kernel path)
```

## executor Backend Mode

The backend is selected via `executor_config::backend`:

```cpp
#include <kmx/aio/readiness/executor.hpp>

// Option 1 – always use epoll (default)
kmx::aio::readiness::executor exec {{
    .backend = kmx::aio::readiness::backend_mode::epoll_only,
}};

// Option 2 – use OpenOnload when the runtime is detected, fall back to epoll silently
kmx::aio::readiness::executor exec {{
    .backend = kmx::aio::readiness::backend_mode::openonload_preferred,
}};

// Option 3 – require OpenOnload; throw std::system_error if the runtime is absent
kmx::aio::readiness::executor exec {{
    .backend = kmx::aio::readiness::backend_mode::openonload_required,
}};

// Query which backend was actually selected after construction
if (exec.get_active_backend() == kmx::aio::readiness::active_backend::openonload)
    // running on the bypass path
```

`openonload_required` throws `std::system_error` with `error_code::openonload_not_available` if the runtime is not detected.

## Runtime Detection

Detection is performed **without linking** to any OpenOnload library. The executor checks three environment variables set by the OpenOnload `LD_PRELOAD` shim or the operator:

| Variable | Meaning |
| :--- | :--- |
| `LD_PRELOAD` containing `"onload"` | OpenOnload shim is preloaded |
| `ONLOAD_STACKNAME` | Explicit stack name is set |
| `EF_POLL_USEC` | Onload spin-polling parameter is set |

If none of these are present, `openonload_preferred` silently selects epoll.

## Zero-Copy Extensions API

All functions are `inline` and live in `kmx::aio::readiness::openonload`. The macro `KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE` (1 or 0) controls whether the real `onload_*` calls are compiled in.

```cpp
#include <kmx/aio/readiness/openonload/extensions.hpp>

namespace oo = kmx::aio::readiness::openonload;

// Register a named stack (call once at process start)
bool ok = oo::initialize_runtime_stack("my_app_fast_stack");

// Check whether a socket fd is on the hardware bypass path
bool accelerated = oo::is_accelerated_fd(sock_fd);

// Zero-copy receive: copies from NIC ring into span, releases hardware buffers immediately
auto result = oo::zero_copy_receive(sock_fd, std::span<char>(buf, len));
if (result)
    std::size_t bytes_read = *result;

// Zero-copy send: allocates NIC transmit buffer, memcpy, pushes to hardware ring
auto send_result = oo::zero_copy_send(sock_fd, std::span<const char>(buf, len));
```

When `KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE == 0`:
- `initialize_runtime_stack()` returns `false`
- `is_accelerated_fd()` returns `false`
- `zero_copy_receive()` and `zero_copy_send()` return `std::errc::function_not_supported`

## TCP Stream Zero-Copy Path

`readiness::tcp::stream::read()` transparently selects the zero-copy path when both conditions hold:

1. The executor reports `active_backend::openonload`
2. `is_accelerated_fd(fd)` returns `true` for the connection's socket

On a ZC error of `ENOTSUP` the implementation falls through to the standard `::read()` kernel call, so the behavior is always correct regardless of whether a given socket is accelerated.

## Error Codes

| Code | Meaning |
| :--- | :--- |
| `error_code::openonload_not_available` | `openonload_required` mode was requested but the runtime was not detected |
| `error_code::openonload_init_failed` | `initialize_runtime_stack()` call returned a non-zero status |

## Build

Enable the feature and build with OpenOnload headers on the include path:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.enable_openonload:true

qbs build -f source/source.qbs config:debug \
    project.enable_openonload:true \
    -j"$(nproc)"
```

OpenOnload headers are detected automatically via `__has_include(<onload/extensions.h>)`. The build succeeds with or without them; `KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE` is set to `0` when the headers are absent.

Install OpenOnload from source on a supported system (Solarflare/Xilinx NIC required at runtime):

```bash
bash script/feature/openonload/install-dependencies.sh
```

> **Note:** OpenOnload requires a Solarflare/Xilinx network adapter at runtime. On systems without a supported NIC, use `backend_mode::openonload_preferred` so the executor falls back to epoll transparently.

## Running with OpenOnload

Launch any readiness-model binary through the OpenOnload shim:

```bash
onload ./my_readiness_server
```

or via LD_PRELOAD directly:

```bash
LD_PRELOAD=libonload.so ./my_readiness_server
```

The executor detects the preloaded shim automatically. No code changes are required when switching between `onload` and bare `epoll`.

To test without a supported NIC use OpenOnload's loopback mode:

```bash
EF_TCP_SERVER_LOOPBACK=2 onload ./my_readiness_server
```

## Source References

| File | Purpose |
| :--- | :--- |
| [source/library/api/kmx/aio/readiness/openonload/extensions.hpp](../../source/library/api/kmx/aio/readiness/openonload/extensions.hpp) | Zero-copy send/receive and fd-check inline functions |
| [source/library/api/kmx/aio/readiness/executor.hpp](../../source/library/api/kmx/aio/readiness/executor.hpp) | `backend_mode`, `active_backend`, `executor_config`, `get_active_backend()` |
| [source/library/src/kmx/aio/readiness/executor.cpp](../../source/library/src/kmx/aio/readiness/executor.cpp) | Runtime detection, backend selection, stack initialization |
| [source/library/src/kmx/aio/readiness/tcp/stream.cpp](../../source/library/src/kmx/aio/readiness/tcp/stream.cpp) | Zero-copy `read()` hot path with epoll fallback |
| [source/library/api/kmx/aio/error_code.hpp](../../source/library/api/kmx/aio/error_code.hpp) | `openonload_not_available`, `openonload_init_failed` error codes |
| [script/feature/openonload/install-dependencies.sh](../../script/feature/openonload/install-dependencies.sh) | Dependency install stub |
| [script/feature/openonload/run-unit-tests.sh](../../script/feature/openonload/run-unit-tests.sh) | Unit test runner (`[openonload]~[integration]`) |
| [script/feature/openonload/run-integration-tests.sh](../../script/feature/openonload/run-integration-tests.sh) | Integration test runner (`[openonload][integration]`) |
