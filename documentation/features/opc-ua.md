# OPC UA

OPC UA integration is feature-gated, disabled by default, and exposed as a backend-neutral facade. In this repository, the backend progress is driven through the completion executor and the open62541/shim boundary.

## Scope

- async facade APIs: client, server, subscription
- open62541 backend compatibility boundary
- feature-off shim for deterministic tests

## Behavior Notes

- Method outputs are surfaced in string form inside `method_call_result.output_arguments`.
- Supported scalar conversion paths include string, signed/unsigned integer, bool, float, and double.
- Unsupported output types map to `UA_STATUSCODE_BADTYPEMISMATCH`.

## C++ Key Methods

OPC UA is exposed as a backend-neutral facade, meaning the identical client, server, and subscription APIs are designed to integrate seamlessly across both readiness (epoll) and completion (io_uring) platforms.

```cpp
kmx::aio::opc_ua::client c(client_cfg);

co_await c.connect();
co_await c.iterate(std::chrono::milliseconds(10));

auto read_res = co_await c.read_node("ns=2;s=Demo.Static.Scalar.String");
co_await c.write_node("ns=2;s=Demo.Static.Scalar.String", "hello");

auto call_res = co_await c.call_method(
    "ns=2;s=Demo.Methods",
    "ns=2;s=Demo.Methods.Add",
    {"1", "2"});

co_await c.disconnect();

kmx::aio::opc_ua::server s(server_cfg);
co_await s.start();
co_await s.iterate(std::chrono::milliseconds(10));
co_await s.stop();

kmx::aio::opc_ua::subscription sub(c, sub_cfg);
co_await sub.open();
auto n = co_await sub.next();
co_await sub.close();
```

## Full C++ Coverage

There is currently no dedicated OPC UA sample project under `source/sample`.
Use the library and tests below as the complete, buildable C++ references:

- [source/library/api/kmx/aio/opc_ua/client.hpp](source/library/api/kmx/aio/opc_ua/client.hpp)
- [source/library/api/kmx/aio/opc_ua/server.hpp](source/library/api/kmx/aio/opc_ua/server.hpp)
- [source/library/api/kmx/aio/opc_ua/subscription.hpp](source/library/api/kmx/aio/opc_ua/subscription.hpp)
- [source/library/src/kmx/aio/opc_ua/client.cpp](source/library/src/kmx/aio/opc_ua/client.cpp)
- [source/library/src/kmx/aio/opc_ua/server.cpp](source/library/src/kmx/aio/opc_ua/server.cpp)
- [source/library/src/kmx/aio/opc_ua/subscription.cpp](source/library/src/kmx/aio/opc_ua/subscription.cpp)
- [source/library-test/src/kmx/aio/opc_ua/client_service_test.cpp](source/library-test/src/kmx/aio/opc_ua/client_service_test.cpp)

## Build and Test

Build with OPC UA enabled:

```bash
bash script/feature/opc_ua/install-dependencies.sh

qbs build -f source/source.qbs config:debug -j"$(nproc)" \
    project.enable_opc_ua:true \
    project.opc_ua_vendored:true \
    project.opc_ua_prefix:"$PWD/build/open62541/install-local"
```

Run fast service tests (excluding slow integration cases):

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "[opc_ua][client][service]~[slow]"
```

Run slow scalar-conversion integration tests:

```bash
"$TEST_BIN" "opc_ua compat async call converts typed outputs"
"$TEST_BIN" "opc_ua compat async call returns bad type mismatch for unsupported output"
```
