# kmx-aio

**kmx-aio** is a modern, high-performance C++26 asynchronous I/O library designed for building non-blocking network applications on Linux. It leverages C++20 coroutines to provide a clean, synchronous-looking API for asynchronous operations, backed by the efficiency of the `epoll` event notification system.

## Key Features

*   **Modern C++26**: Built with the latest language standards.
*   **Coroutine-First Design**: Uses `co_await` for intuitive, sequential async code flow without callback hell.
*   **Edge-Triggered Epoll**: Efficient event notifications for high-performance I/O scalability.
*   **Zero-Overhead Abstractions**: Lightweight wrappers around system calls.
*   **Type-Safe Error Handling**: Extensive use of `std::expected` and `std::error_code` for robust error management.
*   **TCP Networking**: Built-in support for TCP listeners and streams.
*   **UDP Networking**: Dual-layer UDP API — low-level `recvmsg`/`sendmsg` via `udp::socket` and a high-level span/address API via `udp::endpoint`.
*   **Async Timers**: `descriptor::timer` wraps Linux `timerfd` for coroutine-based deadline and interval timers.

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

## Architecture

The library is structured around a central **Executor** and **Task** system:

*   **`kmx::aio::executor`**: The heart of the library. It manages the main event loop, handles `epoll_wait`, and resumes suspended coroutines when I/O events occur.
*   **`kmx::aio::task<T>`**: A lazy-evaluation coroutine type. Tasks are the fundamental unit of asynchronous work.
*   **`kmx::aio::io_base`**: Shared RAII base class for all network I/O objects. Owns the file descriptor and automatically unregisters it from the executor on destruction, guarded by a `weak_ptr` lifetime token.
*   **`kmx::aio::tcp::listener`**: Provides an asynchronous interface for accepting incoming TCP connections.
*   **`kmx::aio::tcp::stream`**: Wraps a connected TCP socket for asynchronous read/write operations.
*   **`kmx::aio::udp::socket`**: Low-level async UDP primitive based on `recvmsg`/`sendmsg`. Intended for use with protocols that manage their own packet framing (e.g. QUIC).
*   **`kmx::aio::udp::endpoint`**: High-level UDP API built on `udp::socket`. Accepts `std::span<std::byte>` payloads and `sockaddr`/`sockaddr_storage` addresses, hiding the `msghdr`/`iovec` plumbing.
*   **`kmx::aio::descriptor::timer`**: RAII wrapper around Linux `timerfd`. Supports one-shot and periodic timers via `set_time()` and `co_await`-able `wait()`.

All TCP and UDP classes inherit `io_base` directly; `tcp::listener`, `tcp::stream`, `udp::socket`, and `udp::endpoint` are move-only (copy-deleted, move-assign-deleted due to the non-reseatable `executor&` member).

## Project Structure

```
kmx-aio/
├── source/
│   ├── library/          # Core library source code
│   │   ├── inc/kmx/aio/  # Public headers
│   │   │   ├── io_base.hpp          # Shared RAII base for all I/O objects
│   │   │   ├── executor.hpp         # Event loop & coroutine scheduler
│   │   │   ├── task.hpp             # Lazy coroutine task<T> type
│   │   │   ├── tcp/                 # TCP listener & stream
│   │   │   ├── udp/                 # UDP socket (low-level) & endpoint (high-level)
│   │   │   └── descriptor/          # File descriptor primitives + timerfd timer
│   │   └── src/                     # Implementation (.cpp) files
│   ├── sample/           # Example applications
│   │   ├── tcp/
│   │   │   ├── minimal/
│   │   │   │   ├── client/          # Minimal TCP client
│   │   │   │   └── server/          # Minimal TCP server
│   │   │   └── echo/
│   │   │       ├── client/          # TCP echo client
│   │   │       └── server/          # TCP echo server
│   │   └── udp/
│   │       ├── minimal/
│   │       │   ├── client/          # Minimal UDP client
│   │       │   └── server/          # Minimal UDP server
│   │       └── echo/
│   │           ├── client/          # UDP echo client
│   │           └── server/          # UDP echo server
│   └── library-test/     # Unit tests
└── build/                # Build artifacts
```

## Usage Examples

### TCP Echo Server

```cpp
#include <kmx/aio/executor.hpp>
#include <kmx/aio/tcp/listener.hpp>
#include <kmx/aio/tcp/stream.hpp>
#include <iostream>

using namespace kmx::aio;

// Coroutine to handle a single client
task<void> handle_client(tcp::stream stream) {
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
task<void> accept_loop(executor& exec) {
    tcp::listener listener(exec, "127.0.0.1", 8080);
    listener.listen();

    while (true) {
        auto accept_result = co_await listener.accept();
        if (accept_result) {
            tcp::stream client_stream(exec, std::move(*accept_result));
            exec.spawn(handle_client(std::move(client_stream)));
        }
    }
}

int main() {
    executor_config cfg{ .thread_count = 1 };
    executor exec(cfg);
    exec.spawn(accept_loop(exec));
    exec.run();
    return 0;
}
```

### UDP Echo Server (high-level API)

```cpp
#include <kmx/aio/executor.hpp>
#include <kmx/aio/udp/endpoint.hpp>
#include <netinet/in.h>
#include <cstring>

using namespace kmx::aio;

task<void> udp_echo(executor& exec) {
    auto ep = udp::endpoint::create(exec, AF_INET);
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
    executor_config cfg{ .thread_count = 1 };
    executor exec(cfg);
    exec.spawn(udp_echo(exec));
    exec.run();
    return 0;
}
```

### One-Shot Timer

```cpp
#include <kmx/aio/executor.hpp>
#include <kmx/aio/descriptor/timer.hpp>
#include <iostream>

using namespace kmx::aio;

task<void> delayed_action(executor& exec) {
    auto tmr = descriptor::timer::create(); // CLOCK_MONOTONIC, non-blocking
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
    executor_config cfg{ .thread_count = 1 };
    executor exec(cfg);
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
