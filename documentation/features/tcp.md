# TCP Networking

TCP support is available in both execution models without any feature gate. The two models offer symmetric listener and stream APIs but differ in how I/O is performed internally.

| | Readiness (epoll) | Completion (io_uring) |
| :--- | :--- | :--- |
| Accept | `epoll` readiness then `::accept4` | `IORING_OP_ACCEPT` submitted to io_uring |
| Read | `epoll` readiness then `::read` (or `onload_zc_recv`) | `IORING_OP_READ` kernel fills buffer directly |
| Write | `epoll` readiness then `::write` | `IORING_OP_WRITE` kernel drains buffer directly |
| Fixed buffers | ❌ | ✅ `read_fixed` / `write_fixed` / `write_all_fixed` |

In the completion model the kernel performs I/O into/from the buffer before the coroutine resumes, eliminating the readiness-then-syscall round-trip. In the readiness model, the TCP hot-path may use `onload_zc_recv` when an OpenOnload executor is active and the fd is hardware-accelerated (see [OpenOnload](openonload.md)).

## Readiness Model

### `readiness::tcp::listener`

```cpp
#include <kmx/aio/readiness/tcp/listener.hpp>

kmx::aio::readiness::executor exec;
kmx::aio::readiness::tcp::listener srv {exec, "0.0.0.0", 8080};
srv.listen(128);                              // set backlog; non-coroutine

while (true)
{
    auto fd = co_await srv.accept();          // suspends until a client connects
    if (!fd)
        break;
    // hand off *fd to a readiness::tcp::stream
}
```

### `readiness::tcp::stream`

```cpp
#include <kmx/aio/readiness/tcp/stream.hpp>

kmx::aio::readiness::tcp::stream conn {exec, std::move(*fd)};

char buf[4096];
auto n = co_await conn.read(std::span{buf});       // suspends until data arrives
if (n && *n > 0)
    co_await conn.write(std::span{buf, *n});       // echo back
```

`write_all` loops internally until all bytes are sent or an error occurs:

```cpp
std::string_view msg = "HTTP/1.1 200 OK\r\n\r\n";
co_await conn.write_all(std::span{msg.data(), msg.size()});
```

## Completion Model

### `completion::tcp::listener`

```cpp
#include <kmx/aio/completion/tcp/listener.hpp>

kmx::aio::completion::executor exec;
kmx::aio::completion::tcp::listener srv {exec, "0.0.0.0", 8080};
srv.listen(128);

while (true)
{
    auto fd = co_await srv.accept();          // IORING_OP_ACCEPT
    if (!fd)
        break;
    // hand off *fd to a completion::tcp::stream
}
```

### `completion::tcp::stream`

```cpp
#include <kmx/aio/completion/tcp/stream.hpp>

kmx::aio::completion::tcp::stream conn {exec, std::move(*fd)};

char buf[4096];
auto n = co_await conn.read(std::span{buf});    // IORING_OP_READ
if (n && *n > 0)
    co_await conn.write(std::span{buf, *n});    // IORING_OP_WRITE
```

### Fixed-buffer operations (completion only)

Fixed buffers are pre-registered with the io_uring instance and avoid per-operation page-pin overhead:

```cpp
// read into a registered buffer at index 0
auto n = co_await conn.read_fixed(std::span{fixed_buf}, 0);

// write from a registered buffer at index 0
co_await conn.write_all_fixed(std::span{fixed_buf, *n}, 0);
```

## Samples

| Sample | Model | Path |
| :--- | :--- | :--- |
| `sample-tcp-minimal-client` | Readiness | `source/sample/readiness/tcp/minimal/client/` |
| `sample-tcp-minimal-server` | Readiness | `source/sample/readiness/tcp/minimal/server/` |
| `sample-tcp-echo-client` | Readiness | `source/sample/readiness/tcp/echo/client/` |
| `sample-tcp-echo-server` | Readiness | `source/sample/readiness/tcp/echo/server/` |
| `sample-tcp-echo-uring-server` | Completion | `source/sample/completion/tcp/echo_uring/server/` |

## Source References

| File | Purpose |
| :--- | :--- |
| [source/library/api/kmx/aio/readiness/tcp/listener.hpp](../../source/library/api/kmx/aio/readiness/tcp/listener.hpp) | Readiness listener |
| [source/library/api/kmx/aio/readiness/tcp/stream.hpp](../../source/library/api/kmx/aio/readiness/tcp/stream.hpp) | Readiness stream |
| [source/library/api/kmx/aio/completion/tcp/listener.hpp](../../source/library/api/kmx/aio/completion/tcp/listener.hpp) | Completion listener |
| [source/library/api/kmx/aio/completion/tcp/stream.hpp](../../source/library/api/kmx/aio/completion/tcp/stream.hpp) | Completion stream (incl. fixed-buffer ops) |
| [source/library/src/kmx/aio/readiness/tcp/stream.cpp](../../source/library/src/kmx/aio/readiness/tcp/stream.cpp) | Readiness stream impl (incl. OpenOnload ZC path) |
