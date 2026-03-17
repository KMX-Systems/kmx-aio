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

Readiness TCP/UDP classes are move-only and inherit `readiness::io_base` (copy-deleted, move-assign-deleted due to the non-reseatable `executor&` member).

## Project Structure

```
kmx-aio/
├── source/
│   ├── library/          # Core library source code
│   │   ├── inc/kmx/aio/  # Public headers
│   │   │   ├── executor_base.hpp    # Shared base for executor state/lifetime controls
│   │   │   ├── file_descriptor.hpp  # RAII file-descriptor wrapper and syscall helpers
│   │   │   ├── task.hpp             # Lazy coroutine task<T> type
│   │   │   ├── readiness/           # epoll model APIs (tcp/udp/tls + descriptor)
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
│   │   │   └── tls/
│   │   │       └── echo_readiness_server/
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
