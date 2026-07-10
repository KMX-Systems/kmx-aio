# QUIC and HTTP/3

QUIC support is provided for both readiness and completion models through:

- `kmx::aio::quic::generic_engine<Executor, UdpSocket>`
- lsquic backend integration

HTTP/3 sample applications are included under completion samples.

## Prerequisites

Build BoringSSL and lsquic before using QUIC/TLS-enabled builds:

```bash
bash script/feature/quic/install-dependencies.sh
```

## Quick Smoke Tests

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "[quic][readiness][integration][smoke]"
"$TEST_BIN" "[quic][http3][integration][smoke]"
```

## Runtime Tuning Variables

- `KMX_AIO_QUIC_READINESS_WATCHDOG_NS` (default `10000000`, valid `1000000..100000000`)
- `KMX_QUIC_ECHO_PORT` (default `12345`)
- `KMX_QUIC_HTTP3_PORT` (default `12345`)

These are useful when running tests in parallel to avoid fixed-port collisions.

## C++ Key Methods - Readiness model

`quic::generic_engine` is parameterized on the executor and UDP-socket type. Readiness instantiation uses the epoll-based executor and socket:

```cpp
kmx::aio::quic::generic_engine<kmx::aio::readiness::executor, kmx::aio::readiness::udp::socket> engine(exec, udp_socket);
auto result = co_await engine.connect(server_name, port);
if (!result)
	co_return;

co_await engine.send(std::span<const std::byte>(payload));
auto recv_result = co_await engine.recv();
```

## C++ Key Methods - Completion model

Completion instantiation uses the io_uring-based executor and socket. All other call sites are identical to the readiness model:

```cpp
kmx::aio::quic::generic_engine<kmx::aio::completion::executor, kmx::aio::completion::udp::socket> engine(exec, udp_socket);
auto result = co_await engine.connect(server_name, port);
if (!result)
	co_return;

co_await engine.send(std::span<const std::byte>(payload));
auto recv_result = co_await engine.recv();
```

## Example

Run completion HTTP/3 server and client samples:

```bash
SERVER_BIN="$(find debug -type f -name sample-quic-http3-server | head -n 1)"
CLIENT_BIN="$(find debug -type f -name sample-quic-http3-client | head -n 1)"
"$SERVER_BIN" &
"$CLIENT_BIN"
```
