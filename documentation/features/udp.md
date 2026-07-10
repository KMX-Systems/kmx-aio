# UDP Networking

UDP support is available in both execution models without any feature gate. Each model exposes two layers: a low-level **socket** wrapping raw `msghdr`/`recvmsg`/`sendmsg`, and a high-level **endpoint** that accepts `std::span<std::byte>` and IP/port pairs.

| | Readiness (epoll) | Completion (io_uring) |
| :--- | :--- | :--- |
| `socket` (low-level) | ✅ `recvmsg` / `sendmsg` | ✅ `recvmsg` / `sendmsg` / `bind` |
| `endpoint` (high-level) | ✅ span + IP/port overloads | ✅ span + IP/port overloads |
| Caller manages `msghdr` | Required for `socket` | Required for `socket` |
| Automatic address decode | ✅ `endpoint` overload | ✅ `endpoint` overload |

## Socket Layer (low-level)

The socket types expose raw `msghdr` for callers that need full control over `iovec`, control messages (cmsg), or interface selection.

### `readiness::udp::socket`

```cpp
#include <kmx/aio/readiness/udp/socket.hpp>

kmx::aio::readiness::executor exec;
auto sock_result = kmx::aio::readiness::udp::socket::create(exec);
auto& sock = *sock_result;

::msghdr msg {};
::iovec iov {buf.data(), buf.size()};
msg.msg_iov = &iov;
msg.msg_iovlen = 1;

auto n = co_await sock.recvmsg(&msg);
auto sent = co_await sock.sendmsg(&msg);
```

### `completion::udp::socket`

```cpp
#include <kmx/aio/completion/udp/socket.hpp>

auto exec = std::make_shared<kmx::aio::completion::executor>();
auto sock_result = kmx::aio::completion::udp::socket::create(exec);
auto& sock = *sock_result;

sock.bind("0.0.0.0", 9000);               // bind before receiving

::msghdr msg {};
// ... fill iov, name, namelen ...
auto n = co_await sock.recvmsg(&msg);
```

## Endpoint Layer (high-level)

`endpoint` wraps a socket and provides span-based recv/send with automatic `sockaddr` construction and optional IP/port decoding.

### `readiness::udp::endpoint`

```cpp
#include <kmx/aio/readiness/udp/socket.hpp>  // pulls in endpoint.hpp

kmx::aio::readiness::executor exec;
auto ep_result = kmx::aio::readiness::udp::endpoint::create(exec);
auto& ep = *ep_result;

std::array<std::byte, 1500> buf;
::sockaddr_storage peer {};
::socklen_t peer_len = sizeof(peer);
kmx::aio::ip_address_t peer_ip;
kmx::aio::port_t peer_port;

// Receive with IP/port decoding
auto n = co_await ep.recv(buf, peer, peer_len, peer_ip, peer_port);

// Echo back using IP/port overload
co_await ep.send(std::span{buf.data(), *n}, peer_ip, peer_port);

// Or send to a raw sockaddr
co_await ep.send(std::span{buf.data(), *n},
                 reinterpret_cast<const sockaddr*>(&peer), peer_len);
```

### `completion::udp::endpoint`

The completion endpoint API is identical to the readiness endpoint but is backed by io_uring `recvmsg`/`sendmsg` operations:

```cpp
#include <kmx/aio/completion/udp/socket.hpp>  // pulls in endpoint.hpp

auto exec = std::make_shared<kmx::aio::completion::executor>();
auto ep_result = kmx::aio::completion::udp::endpoint::create(exec);
auto& ep = *ep_result;

std::array<std::byte, 1500> buf;
::sockaddr_storage peer {};
::socklen_t peer_len = sizeof(peer);

auto n = co_await ep.recv(buf, peer, peer_len);
co_await ep.send(std::span{buf.data(), *n},
                 reinterpret_cast<const sockaddr*>(&peer), peer_len);
```

## Choosing the Right Type

| Use case | Recommended type |
| :--- | :--- |
| Need `cmsg`, `MSG_TRUNC`, interface selection | `socket` |
| Simple send/receive with IP + port | `endpoint` |
| AVB raw Ethernet (not UDP) | `readiness/completion::avb::eth_socket` |

## Samples

| Sample | Model | Path |
| :--- | :--- | :--- |
| `sample-udp-minimal-client` | Readiness | `source/sample/readiness/udp/minimal/client/` |
| `sample-udp-minimal-server` | Readiness | `source/sample/readiness/udp/minimal/server/` |
| `sample-udp-echo-client` | Readiness | `source/sample/readiness/udp/echo/client/` |
| `sample-udp-echo-server` | Readiness | `source/sample/readiness/udp/echo/server/` |
| `sample-udp-echo-uring-server` | Completion | `source/sample/completion/udp/echo_uring/server/` |

## Source References

| File | Purpose |
| :--- | :--- |
| [source/library/api/kmx/aio/readiness/udp/socket.hpp](source/library/api/kmx/aio/readiness/udp/socket.hpp) | Readiness socket |
| [source/library/api/kmx/aio/readiness/udp/endpoint.hpp](source/library/api/kmx/aio/readiness/udp/endpoint.hpp) | Readiness endpoint |
| [source/library/api/kmx/aio/completion/udp/socket.hpp](source/library/api/kmx/aio/completion/udp/socket.hpp) | Completion socket |
| [source/library/api/kmx/aio/completion/udp/endpoint.hpp](source/library/api/kmx/aio/completion/udp/endpoint.hpp) | Completion endpoint |
