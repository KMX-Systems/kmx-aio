# SOME/IP

SOME/IP (Scalable service-Oriented MiddlewarE over IP, [AUTOSAR PRS_SOMEIP](https://www.autosar.org/fileadmin/standards/R22-11/FO/AUTOSAR_PRS_SOMEIPProtocol.pdf)) integration is feature-gated, disabled by default, and exposed as a backend-neutral facade. The transport backend is [vsomeip](https://github.com/COVESA/vsomeip) when available; a deterministic in-process stub activates automatically when vsomeip headers are absent, enabling full unit-test coverage without a real daemon.

## Scope

- Async coroutine facades: `client`, `server`, `subscription`
- vsomeip compatibility boundary (`compat::client_runtime`, `compat::server_runtime`)
- In-process stub backend for deterministic testing
- Echo server and echo client samples

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  User code  (co_await client::call_method / server::next_request)  │
└────────────────────────┬────────────────────────────────────┘
                         │ kmx::aio::someip facade
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  compat::client_runtime / compat::server_runtime            │
│  (internal; not part of the public API)                     │
├──────────────────────────────┬──────────────────────────────┤
│   Stub backend               │   vsomeip backend            │
│   (no vsomeip headers)       │   (KMX_AIO_SOMEIP_LINK_BACKEND) │
│   – deterministic in-process │   – real IPC via vsomeip     │
│   – test injection hooks     │   – Service Discovery (SD)   │
└──────────────────────────────┴──────────────────────────────┘
```

The backend is selected at compile time:

- **Stub** (default when `project.someip_link_backend:false`): all operations succeed immediately in-process; `__kmx_test_push_event` is available for injecting synthetic notifications.
- **vsomeip** (`project.someip_link_backend:true`): vsomeip's application thread, Service Discovery, and real IPC are activated. vsomeip headers must be on the include path.

## Key Types (`kmx::aio::someip`)

| Type | Purpose |
| :--- | :--- |
| `service_id_t` | 16-bit SOME/IP service identifier |
| `instance_id_t` | 16-bit service instance identifier |
| `method_id_t` | 16-bit method identifier |
| `event_id_t` | 16-bit event identifier |
| `event_group_id_t` | 16-bit event group identifier |
| `request_id_t` | 32-bit composite (client-id \| session-id) |
| `client_config` | Application name, timeouts, reconnect policy |
| `server_config` | Application name, service coordinates, iterate timeout |
| `subscription_config` | Service/instance/event-group coordinates, queue capacity |
| `call_result` | Echoed IDs + response payload from `call_method` |
| `method_request` | Incoming RPC request received by `server::next_request` |
| `event_notification` | Single buffered event delivered by `subscription::next` |
| `statistics` | Operational counters (starts, calls sent/received, dropped events) |

## Error Codes (`kmx::aio::someip::error`)

| Enumerator | Meaning |
| :--- | :--- |
| `success` | Operation completed successfully |
| `feature_disabled` | Feature gate is disabled at build time |
| `not_initialized` | Object has not been started yet |
| `invalid_configuration` | Required field is missing or invalid |
| `start_failed` | Runtime failed to initialise or register |
| `stopped` | Runtime is already stopped |
| `service_not_found` | Service discovery returned no match |
| `service_unavailable` | Service exists but is currently unreachable |
| `request_failed` | Request/offer rejected by the backend |
| `response_failed` | Sending a response back to the caller failed |
| `subscription_closed` | Subscription is not open |
| `timed_out` | Operation exceeded the configured timeout |
| `internal_error` | Unexpected backend error |

All enumerators integrate with `std::error_code` via the `std::is_error_code_enum` specialization.

## C++ API

### Client

```cpp
#include <kmx/aio/someip/client.hpp>

kmx::aio::someip::client cli {{
    .application_name = "my_client",
    .config_file_path = "",          // empty = vsomeip default
    .service_id       = 0x1111u,
    .instance_id      = 0x2222u,
    .connect_timeout  = std::chrono::milliseconds(5000),
}};

// Start the vsomeip runtime
co_await cli.start();

// Register interest in a service/instance
co_await cli.request_service(0x1111u, 0x2222u);

// Issue a remote method call and wait for the response
const std::vector<std::uint8_t> payload = {'p', 'i', 'n', 'g'};
auto result = co_await cli.call_method(0x1111u, 0x2222u, 0x3333u, payload);
if (result)
    // result->payload contains the response bytes

// Graceful shutdown
co_await cli.stop();
```

### Server

```cpp
#include <kmx/aio/someip/server.hpp>

kmx::aio::someip::server srv {{
    .application_name = "my_server",
    .config_file_path = "",
    .service_id       = 0x1111u,
    .instance_id      = 0x2222u,
}};

co_await srv.start();
co_await srv.offer_service(0x1111u, 0x2222u);

// Dispatch loop
for (int i = 0; i < 200; ++i)
{
    auto req = co_await srv.next_request();
    if (req)
        co_await srv.send_response(req->request_id, req->payload);

    co_await srv.iterate(std::chrono::milliseconds(10));
}

co_await srv.stop_offer_service(0x1111u, 0x2222u);
co_await srv.stop();
```

### Subscription (event notifications)

```cpp
#include <kmx/aio/someip/subscription.hpp>

kmx::aio::someip::client cli { client_cfg };
co_await cli.start();

kmx::aio::someip::subscription sub {cli, {
    .service_id                  = 0x1111u,
    .instance_id                 = 0x2222u,
    .event_group_id              = 0x1000u,
    .event_ids                   = {0x1001u},
    .notification_queue_capacity = 64u,
    .iterate_timeout             = std::chrono::milliseconds(50),
}};

co_await sub.open();

while (true)
{
    auto notification = co_await sub.next();
    if (!notification)
        break;   // timed_out or closed

    // notification->payload, notification->event_id, notification->source_timestamp
}

co_await sub.close();
```

`subscription` buffers notifications up to `notification_queue_capacity`; when the buffer is full the oldest entry is evicted and `dropped_events()` is incremented. Setting capacity to `0` discards all incoming notifications.

## Behaviour Notes

- `client`, `server`, and `subscription` each own an independent vsomeip application instance and dispatch thread; they must not be shared across threads without external synchronization.
- `iterate()` is a polling primitive that drives the internal loop for one time slot and is sufficient for liveness in a tight coroutine loop.
- Service availability in the stub backend is tracked locally via `request_service` / `release_service`, mirroring vsomeip semantics without a real daemon.
- `statistics::dropped_events` on `client` is lazy-synchronised from the backend on each `get_stats()` call.
- `subscription::dropped_events()` accumulates both the facade-level counter and the compat-layer counter across the subscription lifetime; it resets on `close()` + `open()`.

## Build

Enable the feature and resolve the build graph:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.enable_someip:true

qbs build -f source/source.qbs config:debug \
    project.enable_someip:true \
    -j"$(nproc)"
```

To link the real vsomeip backend instead of the stub:

```bash
qbs build -f source/source.qbs config:debug \
    project.enable_someip:true \
    project.someip_link_backend:true \
    project.someip_vendored:true \
    project.someip_prefix:"$PWD/build/someip/install-local" \
    -j"$(nproc)"
```

Install vsomeip from source (builds and installs into `build/someip/install-local`):

```bash
bash script/feature/someip/install-dependencies.sh
```

## Testing

Run unit tests (stub backend only):

```bash
bash script/feature/someip/run-unit-tests.sh
```

Run integration smoke test (launches echo-server and echo-client):

```bash
bash script/feature/someip/run-smoke.sh
```

Run integration tests via Catch2 filter directly:

```bash
TEST_BIN="$(find source/debug -type f -name kmx-aio-test | head -n 1)"

# Unit tests (all SOME/IP, excluding integration)
"$TEST_BIN" "[someip]~[integration]"

# Integration smoke test
"$TEST_BIN" "[someip][integration]"
```

Skip the build step when the binary is already up to date:

```bash
bash script/feature/someip/run-smoke.sh --skip-build
```

## Source References

| File | Purpose |
| :--- | :--- |
| [source/library/api/kmx/aio/someip/types.hpp](source/library/api/kmx/aio/someip/types.hpp) | All public types and configuration structs |
| [source/library/api/kmx/aio/someip/error.hpp](source/library/api/kmx/aio/someip/error.hpp) | Error domain and `make_error_code` |
| [source/library/api/kmx/aio/someip/client.hpp](source/library/api/kmx/aio/someip/client.hpp) | Client facade |
| [source/library/api/kmx/aio/someip/server.hpp](source/library/api/kmx/aio/someip/server.hpp) | Server facade |
| [source/library/api/kmx/aio/someip/subscription.hpp](source/library/api/kmx/aio/someip/subscription.hpp) | Subscription facade |
| [source/library/inc/kmx/aio/someip/vsomeip_compat.hpp](source/library/inc/kmx/aio/someip/vsomeip_compat.hpp) | Internal compat layer (stub + vsomeip) |
| [source/library/src/kmx/aio/someip/vsomeip_compat.cpp](source/library/src/kmx/aio/someip/vsomeip_compat.cpp) | Stub and vsomeip backend implementations |
| [source/library/src/kmx/aio/someip/client.cpp](source/library/src/kmx/aio/someip/client.cpp) | Client facade implementation |
| [source/library/src/kmx/aio/someip/server.cpp](source/library/src/kmx/aio/someip/server.cpp) | Server facade implementation |
| [source/library/src/kmx/aio/someip/subscription.cpp](source/library/src/kmx/aio/someip/subscription.cpp) | Subscription facade implementation |
| [source/library-test/src/kmx/aio/someip/client_service_test.cpp](source/library-test/src/kmx/aio/someip/client_service_test.cpp) | Client start/stop/call unit tests |
| [source/library-test/src/kmx/aio/someip/subscription_test.cpp](source/library-test/src/kmx/aio/someip/subscription_test.cpp) | Subscription queue and lifecycle unit tests |
| [source/library-test/src/kmx/aio/someip/compat_queue_test.cpp](source/library-test/src/kmx/aio/someip/compat_queue_test.cpp) | Compat-layer queue overflow/capacity unit tests |
| [source/library-test/src/kmx/aio/someip/error_test.cpp](source/library-test/src/kmx/aio/someip/error_test.cpp) | Error category and message tests |
| [source/library-test/src/kmx/aio/integration/someip_smoke_test.cpp](source/library-test/src/kmx/aio/integration/someip_smoke_test.cpp) | Echo server/client integration smoke test |
| [source/sample/completion/someip/echo-server/src/main.cpp](source/sample/completion/someip/echo-server/src/main.cpp) | Echo server sample |
| [source/sample/completion/someip/echo-client/src/main.cpp](source/sample/completion/someip/echo-client/src/main.cpp) | Echo client sample |
| [script/feature/someip/install-dependencies.sh](script/feature/someip/install-dependencies.sh) | vsomeip build and install script |
| [script/feature/someip/run-unit-tests.sh](script/feature/someip/run-unit-tests.sh) | Unit test runner |
| [script/feature/someip/run-integration-tests.sh](script/feature/someip/run-integration-tests.sh) | Integration test runner |
| [script/feature/someip/run-smoke.sh](script/feature/someip/run-smoke.sh) | Full smoke: build + tests + sample echo |
