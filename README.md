# kmx-aio

**kmx-aio** is a modern, high-performance C++26 asynchronous I/O library designed for building non-blocking network applications on Linux. It leverages C++20 coroutines to provide a clean, synchronous-looking API for asynchronous operations, backed by the efficiency of the `epoll` event notification system.

## Key Features

*   **Modern C++26**: Built with the latest language standards.
*   **Coroutine-First Design**: Uses `co_await` for intuitive, sequential async code flow without callback hell.
*   **Edge-Triggered Epoll**: Efficient event notifications for high-performance I/O scalability.
*   **Zero-Overhead Abstractions**: Lightweight wrappers around system calls.
*   **Type-Safe Error Handling**: Extensive use of `std::expected` and `std::error_code` for robust error management.
*   **TCP Networking**: Built-in support for TCP listeners and streams.

## Requirements

*   **Operating System**: Linux (requires `sys/epoll.h`).
*   **Compiler**: A C++ compiler supporting C++26 features (e.g., recent GCC or Clang).
*   **Build System**: QBS (Qt Build Suite).

## Architecture

The library is structured around a central **Executor** and **Task** system:

*   **`kmx::aio::executor`**: The heart of the library. It manages the main event loop, handles `epoll_wait`, and resumes suspended coroutines when I/O events occur.
*   **`kmx::aio::task<T>`**: A lazy-evaluation coroutine type. Tasks are the fundamental unit of asynchronous work.
*   **`kmx::aio::tcp::listener`**: Provides an asynchronous interface for accepting incoming TCP connections.
*   **`kmx::aio::tcp::stream`**: Wraps a connected socket for asynchronous read/write operations.

## Project Structure

```
kmx-aio/
├── source/
│   ├── library/          # Core library source code
│   │   ├── inc/kmx/aio/  # Public headers (Executor, Task, TCP, Descriptors)
│   │   └── src/          # Implementation details
│   ├── sample/           # Example applications
│   │   ├── client/       # Stress test client
│   │   └── server/       # Echo server implementation
│   └── library-test/     # Unit tests
└── build/                # Build artifacts
```

## Usage Example

Here is a simplified example of how to write an echo server using `kmx-aio`.

### Echo Server

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
            // Convert file descriptor to stream and spawn handler
            tcp::stream client_stream(exec, std::move(*accept_result));
            exec.spawn(handle_client(std::move(client_stream)));
        }
    }
}

int main() {
    executor_config cfg{ .thread_count = 1 };
    executor exec(cfg);

    // Spawn the main acceptor task
    exec.spawn(accept_loop(exec));

    // Run the event loop
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

## License

(Include License Information Here)
