# Modbus

Modbus support is feature-gated (`project.enable_modbus:true`) and currently implemented on the readiness stack. The module provides coroutine-friendly Modbus TCP and Modbus/TLS client/server APIs, plus framing utilities and deterministic unit/integration tests.

## Scope

- Modbus TCP client API (`client`) and server API (`server`)
- Modbus/TLS client API (`tls_client`) and server API (`tls_server`) with optional mTLS
- Protocol framing helpers (`frame.hpp`) for MBAP/PDU encode/decode
- Shared exchange/session helpers (`detail/session.hpp`)
- Unit and integration coverage, including TLS negative-path validation

## Build

Enable Modbus (readiness is auto-enabled by script helpers when needed):

```bash
qbs resolve -f source/source.qbs config:debug \
    project.enable_modbus:true

qbs build -f source/source.qbs config:debug -j"$(nproc)" \
    project.enable_modbus:true
```

## API Entry Points

- `kmx::aio::modbus::client`
- `kmx::aio::modbus::server`
- `kmx::aio::modbus::tls_client`
- `kmx::aio::modbus::tls_server`
- `kmx::aio::modbus::frame::*`

## Test Commands

Run Modbus unit tests:

```bash
TEST_BIN="$(find source/debug -type f -name kmx-aio-test | head -n 1)"
timeout 25s "$TEST_BIN" "[modbus]~[integration]"
```

Run Modbus integration tests via feature script (includes TLS cert bootstrap and isolated TLS cases):

```bash
bash script/feature/modbus/run-integration-tests.sh
```

Run specific TLS scenarios directly:

```bash
TEST_BIN="$(find source/debug -type f -name kmx-aio-test | head -n 1)"
timeout 25s "$TEST_BIN" "modbus tls: mTLS client and server exchange registers"
timeout 25s "$TEST_BIN" "modbus tls: server rejects client with missing certificate"
```

## Feature Scripts

- `script/feature/modbus/install-dependencies.sh`
  - Emits an informational message; no extra system packages are required.
- `script/feature/modbus/run-unit-tests.sh`
  - Runs Catch2 filter: `[modbus]~[integration]`.
- `script/feature/modbus/run-integration-tests.sh`
  - Generates/reuses cert sets in `/tmp/kmx_modbus_certs_exchange` and `/tmp/kmx_modbus_certs_reject`.
  - Runs non-TLS integration filter first: `[modbus][integration]~[tls]`.
  - Runs TLS integration cases in separate test process invocations to avoid cross-test runtime interference.

## Source References

- `source/library/api/kmx/aio/modbus/client.hpp`
- `source/library/api/kmx/aio/modbus/server.hpp`
- `source/library/api/kmx/aio/modbus/tls_client.hpp`
- `source/library/api/kmx/aio/modbus/tls_server.hpp`
- `source/library/api/kmx/aio/modbus/frame.hpp`
- `source/library/api/kmx/aio/modbus/detail/session.hpp`
- `source/library/src/kmx/aio/modbus/client.cpp`
- `source/library/src/kmx/aio/modbus/server.cpp`
- `source/library/src/kmx/aio/modbus/tls_client.cpp`
- `source/library/src/kmx/aio/modbus/tls_server.cpp`
- `source/library-test/src/kmx/aio/modbus/client_unit_test.cpp`
- `source/library-test/src/kmx/aio/modbus/integration/client_server_test.cpp`
- `source/library-test/src/kmx/aio/modbus/integration/tls_client_server_test.cpp`
