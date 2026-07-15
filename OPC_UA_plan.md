## Plan: Async OPC UA Support for kmx-aio

Deliver comprehensive OPC UA support based on open62541 in two explicit tracks:
1. a low-risk, production-usable async wrapper layer that gives kmx-aio users coroutine-friendly OPC UA client/server/subscription/PubSub APIs while open62541 runs in its own controlled iteration loop, and
2. an optional deeper native transport integration track that replaces open62541's default TCP event plumbing with a kmx-aio-backed `UA_EventLoop` + `UA_ConnectionManager`.

This split is the main improvement to the previous plan. It reduces schedule risk, gives a credible first delivery, and acknowledges that open62541's `UA_EventLoop` surface is broader than just timers and a run callback: it also requires lifecycle, cancellation, delayed callbacks, event-source registration, and recursive locking semantics.

**Recommendation**
Ship Track A first. Keep Track B as a second milestone once Track A proves the public API, performance envelope, and operational model.

### Why this change is necessary

Repository and upstream review surfaced these planning corrections:
- In [source/source.qbs](source/source.qbs), most existing feature gates default to `true`, not `false`. The plan should call out that adding OPC UA as opt-in is a deliberate policy change, not a repository norm.
- In [source/library/lib.qbs](source/library/lib.qbs), the project already relies heavily on broad source globs. Explicit OPC UA source globs may still be desirable, but they are a reliability choice, not a current global convention.
- In [source/library/api/kmx/aio/error_code.hpp](source/library/api/kmx/aio/error_code.hpp), the error enum is intentionally compact. Adding many OPC UA-specific enum members would expand a currently small cross-library error vocabulary. The first milestone should prefer a dedicated OPC UA error category layered on top of `std::error_code`, not large enum growth.
- open62541's `UA_EventLoop` contract is larger than originally described. A custom implementation must provide `start`, `stop`, `free`, `run`, `cancel`, clock functions, timer APIs, delayed callback APIs, event-source registration/deregistration, and external recursive locking hooks. That makes native EventLoop replacement a second-stage project, not the starting point.
- For Track A, open62541 already supports external main-loop iteration through `UA_Server_run_startup`, `UA_Server_run_iterate`, `UA_Server_run_shutdown`, and async client APIs such as `UA_Client_connectAsync` and `UA_Client_sendAsyncReadRequest`. That is enough to build a strong async wrapper without first replacing the transport core.

---

## Architecture decision

### Track A — Async wrapper around stock open62541 eventing

This track does **not** replace the open62541 EventLoop or ConnectionManager.

Instead:
- kmx-aio owns the coroutine-facing API surface.
- open62541 keeps its internal network/event implementation.
- a kmx-aio task periodically drives `UA_Server_run_iterate(server, waitInternal)` or `UA_Client_run_iterate(client, timeout)`.
- requests, subscriptions, reconnect loops, and user-facing cancellation are expressed as `task<std::expected<...>>` and `channel<T>` abstractions in kmx-aio.
- blocking or timing behavior is controlled by where and how the wrapper iterates open62541, not by reimplementing the transport layer.

This yields:
- coroutine-friendly APIs quickly
- lower implementation risk
- easier verification against upstream examples/tests
- a stable surface for applications even if Track B changes internals later

### Track B — Native kmx-aio-backed transport integration

This track injects a custom `UA_EventLoop` and `UA_ConnectionManager` so open62541 network I/O is driven by kmx-aio executors and transport objects.

This is feasible, but it is materially larger because the custom EventLoop must implement:
- lifecycle: `start`, `stop`, `free`, `run`, `cancel`
- clocks: `dateTime_now`, `dateTime_nowMonotonic`, `dateTime_localTimeUtcOffset`
- timer APIs: `nextTimer`, `addTimer`, `modifyTimer`, `removeTimer`
- delayed callbacks: `addDelayedCallback`, `removeDelayedCallback`
- event-source list management: `registerEventSource`, `deregisterEventSource`
- recursive locking hooks: `lock`, `unlock`

That complexity makes it the right second milestone, not the first.

---

## Scope and milestones

### Milestone 1 — Async OPC UA wrapper layer

Deliver a complete public API for:
- secure client connect/disconnect
- secure server lifecycle
- read/write services
- method calls
- subscriptions/monitored items
- PubSub configuration and consumption helpers
- reconnect loops
- stats/diagnostics

This milestone uses stock open62541 transport and eventing.

### Milestone 2 — Native kmx-aio transport integration

Deliver:
- custom `UA_EventLoop`
- custom TCP `UA_ConnectionManager` backed by completion executor first
- optional readiness parity afterwards
- transport-level performance and shutdown improvements

### Milestone 3 — Backend parity and optimization

Deliver:
- readiness parity for Track B if still justified
- buffer registration / zero-copy experiments where applicable
- more aggressive throughput targets

---

## Phase 0 — Baseline and compatibility lock

1. Lock initial upstream target to open62541 v1.4.x.
Reason: stable API, mature encryption support, and broad distro availability.

2. Publish compatibility baseline.
- Linux target: current repo baseline plus `io_uring` availability only for Track B completion work.
- Security baseline: `Basic256Sha256` + `SignAndEncrypt` by default.
- Track A does not require io_uring at all and can run even if native completion integration is deferred.

3. Define explicit success criteria per track.
- Track A success: applications can use coroutine APIs without directly touching open62541 iteration loops.
- Track B success: open62541 network transport is driven by kmx-aio executors without regressions in lifecycle or security behavior.

---

## Phase 1 — Build and dependency plan

1. Add `enable_opc_ua` to [source/source.qbs](source/source.qbs).
Decision point: default should be explicitly chosen.
Recommendation: default to `false` because OPC UA introduces a large external dependency and optional feature families in this repo are not uniform in practice.

2. Add dependency handling to [source/library/lib.qbs](source/library/lib.qbs).
Support two modes:
- system package mode using `pkg-config` when available
- vendored install mode via a helper script

3. Add vendored installer script in `build/install_open62541.sh`.
Recommended initial flags:
- `UA_ENABLE_ENCRYPTION=OPENSSL`
- `UA_ENABLE_SUBSCRIPTIONS=ON`
- `UA_ENABLE_PUBSUB=ON`
- `UA_ENABLE_PUBSUB_MQTT=ON` only if MQTT is required in milestone 1
- `BUILD_SHARED_LIBS=OFF` for reproducibility

4. Keep source registration simple.
Because [source/library/lib.qbs](source/library/lib.qbs) already uses broad globs, prefer placing OPC UA sources under existing globbed trees unless linker behavior proves brittle. If brittle, then add explicit globs only for the new OPC UA `.cpp` trees.

5. Add license handling note.
Document MPLv2 implications in build/docs, but do not overcomplicate the plan with legal mechanics beyond dependency provenance and separation of wrapper code.

---

## Phase 2 — Milestone 1: async wrapper layer

### 2A — Public API design

Add a public namespace family under `kmx::aio::opc_ua` plus backend-neutral wrapper types.

Recommended public surface:
- `kmx::aio::opc_ua::client`
- `kmx::aio::opc_ua::server`
- `kmx::aio::opc_ua::subscription`
- `kmx::aio::opc_ua::pubsub_publisher`
- `kmx::aio::opc_ua::pubsub_subscriber`
- `kmx::aio::opc_ua::client_config`
- `kmx::aio::opc_ua::server_config`
- `kmx::aio::opc_ua::subscription_config`
- `kmx::aio::opc_ua::statistics`

Key design improvement:
These APIs should be backend-neutral in Milestone 1 because they do not yet depend on completion/readiness transport replacement.

### 2B — Wrapper execution model

Implement a kmx-aio-managed driver task per client/server instance.

Server pattern:
- `UA_Server_new`
- config setup via `UA_ServerConfig_setDefaultWithSecurityPolicies`
- `UA_Server_run_startup`
- loop: `UA_Server_run_iterate(server, waitInternal)` inside a kmx task
- on stop: `UA_Server_run_shutdown`, `UA_Server_delete`

Client pattern:
- `UA_Client_new`
- config setup via `UA_ClientConfig_setDefaultEncryption`
- `UA_Client_connectAsync`
- loop: `UA_Client_run_iterate(client, timeout_ms)` inside a kmx task until activated or failed
- reconnect policy handled by wrapper task

Important refinement:
This phase should avoid claiming that open62541 itself is driven by kmx executors at the socket level. Instead, kmx drives open62541 at the iteration level.

### 2C — Request/response service wrappers

Use open62541 async client APIs where available:
- `UA_Client_sendAsyncReadRequest`
- async write/call equivalents through generic async service path

Expose kmx-friendly wrappers returning `task<std::expected<...>>`.

Implementation model:
- register upstream callback with request ID
- store pending request state in a map keyed by request ID
- callback fulfills state and wakes suspended coroutine
- coroutine awaits completion using either a dedicated awaiter or `channel`/event primitive

### 2D — Subscription layer

Use [source/library/api/kmx/aio/channel.hpp](source/library/api/kmx/aio/channel.hpp) directly for subscription fan-out.

Refinement from previous plan:
`channel<T>` is SPSC only. Therefore the plan must state that each subscription notification stream is owned by exactly one producer and one consumer, or else a different queue abstraction is required.

Recommended choice for Milestone 1:
- one producer: open62541 callback adapter
- one consumer: application-facing subscription task
- if multi-consumer fan-out is required later, add a dispatcher layer above `channel<T>` rather than changing the core plan now

### 2E — PubSub layer

Do not promise custom transport integration in Milestone 1.

Use open62541 PubSub APIs on their native supported connection managers.
Provide wrapper abstractions for:
- publisher configuration
- subscriber setup
- received message delivery through kmx channels/tasks

Decision boundary:
If MQTT materially increases build and runtime complexity, split PubSub into:
- Milestone 1A: UADP/UDP only
- Milestone 1B: MQTT support

### 2F — Error model

This is a major plan correction.

Do **not** expand [source/library/api/kmx/aio/error_code.hpp](source/library/api/kmx/aio/error_code.hpp) with a large set of OPC UA-specific members in the first pass.

Instead:
- create an OPC UA-specific `std::error_category`
- preserve raw `UA_StatusCode` in that category
- convert wrapper failures to `std::error_code` from that category
- keep existing `kmx::aio::error_code` only for transport/backend failures in kmx-owned code

This keeps the existing compact error taxonomy stable.

### 2G — Statistics and observability

Expose wrapper-level metrics first:
- connect attempts
- successful connects
- reconnects
- read/write/call request counts
- subscription notifications delivered
- notification drops due to queue pressure
- PubSub messages received/sent
- certificate validation failures

These should live in a dedicated OPC UA stats struct, not in executor stats.

---

## Phase 3 — Milestone 1 tests and samples

1. Add unit/integration coverage through [source/library-test/unit-test.qbs](source/library-test/unit-test.qbs).

Recommended test groups:
- client connect/activate/disconnect
- secure handshake success/failure
- async read/write/call wrappers
- subscription delivery and backpressure behavior
- reconnect loop behavior
- PubSub smoke tests

2. Sample layout.
Register new samples in the existing sample aggregation points:
- [source/sample/completion/completion.qbs](source/sample/completion/completion.qbs) if the sample explicitly demonstrates completion-oriented orchestration
- [source/sample/readiness/readiness.qbs](source/sample/readiness/readiness.qbs) only when there is an actual readiness-specific implementation

Refinement:
For Milestone 1, samples should likely live under a neutral OPC UA area rather than pretending backend-specific transport integration exists already.

Recommended samples:
- secure server
- secure client read/write
- method call client
- subscription client
- PubSub UADP example

3. Verification targets for Milestone 1.
- secure client/server roundtrip
- certificate rejection path
- long-lived subscription
- reconnect after forced disconnect
- clean shutdown with active requests

---

## Phase 4 — Milestone 2: native kmx-aio transport integration

Only start this after Milestone 1 is stable.

### 4A — Custom `UA_ConnectionManager` backed by completion executor

Build a TCP connection manager using real kmx transport types:
- [source/library/api/kmx/aio/completion/executor.hpp](source/library/api/kmx/aio/completion/executor.hpp)
- [source/library/api/kmx/aio/completion/tcp/stream.hpp](source/library/api/kmx/aio/completion/tcp/stream.hpp)
- [source/library/api/kmx/aio/completion/tcp/listener.hpp](source/library/api/kmx/aio/completion/tcp/listener.hpp)

Implementation details should reference actual signatures:
- `executor::async_connect(fd, addr, addrlen)`
- `executor::async_read(fd, span<char>)`
- `executor::async_write(fd, span<const char>)`
- `listener::accept()` returning `task<std::expected<file_descriptor, std::error_code>>`

Correction to the previous plan:
The earlier version modeled connection state around storing `completion::tcp::stream` directly, but `listener::accept()` yields an owned descriptor and stream construction is a wrapper step. The plan should preserve that real layering.

### 4B — Custom `UA_EventLoop`

This phase must explicitly implement the full EventLoop interface, not a thin timer shim.

Required surface:
- lifecycle: `start`, `stop`, `free`, `run`, `cancel`
- clocks: `dateTime_now`, `dateTime_nowMonotonic`, `dateTime_localTimeUtcOffset`
- timer functions: `nextTimer`, `addTimer`, `modifyTimer`, `removeTimer`
- delayed callbacks: `addDelayedCallback`, `removeDelayedCallback`
- event source management: `registerEventSource`, `deregisterEventSource`
- recursive lock/unlock

Plan refinement:
This is best implemented by borrowing the structural model from open62541's own POSIX event loop rather than inventing a minimal adapter.

### 4C — Network buffer ownership

The previous plan proposed `buffer_pool<std::array<std::byte,65536>, 64>`.
That is plausible, but the ownership model should be validated against the actual API of [source/library/api/kmx/aio/buffer_pool.hpp](source/library/api/kmx/aio/buffer_pool.hpp).

Refinement:
- `buffer_pool` returns `buffer_handle<T>` RAII objects
- open62541 expects `allocNetworkBuffer` / `freeNetworkBuffer` pointer-style ownership
- therefore the plan should include an explicit adapter layer that maps open62541 buffer lifetime to retained pool handles, not just raw pointer assignment

This is an important missing detail.

### 4D — Completion-only first

Do not start readiness parity until completion transport is working.
The completion path is the more novel one and has the greatest payoff.

---

## Phase 5 — Milestone 3: readiness parity and optimization

1. Port native transport integration to [source/library/api/kmx/aio/readiness/executor.hpp](source/library/api/kmx/aio/readiness/executor.hpp) only if Track B is justified.

2. Use the real readiness contracts:
- `register_fd`
- `wait_io(fd, event_type)`
- subscriber-driven coroutine resumption

3. Document backend differences rather than forcing false parity.
Examples:
- completion may support registered-buffer optimizations
- readiness may have simpler debugging behavior
- some performance targets may differ

---

## Detailed file plan

**Build and config**
- [source/source.qbs](source/source.qbs) — add `enable_opc_ua` and optional vendored-mode property
- [source/library/lib.qbs](source/library/lib.qbs) — dependency wiring and compile define
- `build/install_open62541.sh` — vendored install flow

**Existing abstractions to reuse**
- [source/library/api/kmx/aio/task.hpp](source/library/api/kmx/aio/task.hpp) — coroutine wrapper and stop token integration
- [source/library/api/kmx/aio/channel.hpp](source/library/api/kmx/aio/channel.hpp) — SPSC subscription/event delivery
- [source/library/api/kmx/aio/buffer_pool.hpp](source/library/api/kmx/aio/buffer_pool.hpp) — only for Track B buffer ownership adaptation
- [source/library/api/kmx/aio/completion/executor.hpp](source/library/api/kmx/aio/completion/executor.hpp) — Track B completion transport
- [source/library/api/kmx/aio/completion/tcp/stream.hpp](source/library/api/kmx/aio/completion/tcp/stream.hpp) — Track B TCP stream wrapper
- [source/library/api/kmx/aio/completion/tcp/listener.hpp](source/library/api/kmx/aio/completion/tcp/listener.hpp) — Track B accept path
- [source/library/api/kmx/aio/readiness/executor.hpp](source/library/api/kmx/aio/readiness/executor.hpp) — Track B readiness port
- [source/library/api/kmx/aio/error_code.hpp](source/library/api/kmx/aio/error_code.hpp) — keep compact; avoid broad OPC UA-specific enum growth
- [source/library/inc/kmx/aio/quic/base_engine.hpp](source/library/inc/kmx/aio/quic/base_engine.hpp) — reuse as a pattern only where callback-to-coroutine bridging matches

**Tests and samples**
- [source/library-test/unit-test.qbs](source/library-test/unit-test.qbs)
- [source/sample/sample.qbs](source/sample/sample.qbs)
- [source/sample/completion/completion.qbs](source/sample/completion/completion.qbs)
- [source/sample/readiness/readiness.qbs](source/sample/readiness/readiness.qbs)

---

## Verification plan

### Milestone 1 verification
1. Build succeeds with OPC UA disabled.
2. Build succeeds with system open62541 enabled.
3. Secure client/server connection succeeds using wrapper APIs.
4. Async read/write/call wrappers complete correctly.
5. Subscription stream survives sustained notification load with documented SPSC limits.
6. Reconnect loop recovers after forced disconnect.
7. Clean shutdown works with active requests.

### Milestone 2 verification
1. Custom completion-backed ConnectionManager passes connect/send/close lifecycle tests.
2. Custom EventLoop passes timer, delayed-callback, event-source lifecycle, and cancel tests.
3. Secure client/server behavior matches Milestone 1 functional behavior.
4. Buffer ownership adapter does not leak or double-free pooled memory.

### Milestone 3 verification
1. Readiness-backed native integration reaches functional parity targets that are explicitly in scope.
2. Backend-specific behavior differences are documented and tested.

---

## Decisions
- First delivery should be the async wrapper layer, not native EventLoop replacement.
- OPC UA public APIs should be backend-neutral in Milestone 1.
- `std::error_category` for OPC UA status codes is preferred over large expansion of `kmx::aio::error_code`.
- Track B native transport work is completion-first.
- PubSub should be split into UADP-first and MQTT-second unless MQTT is an immediate hard requirement.
- `channel<T>` SPSC semantics must be respected explicitly in subscription design.

## Open items worth deciding early
1. Should `enable_opc_ua` default to `false` even though several existing feature toggles default to `true`?
Recommendation: yes.
2. Is MQTT required in the first deliverable or can PubSub be UADP/UDP-first?
Recommendation: UADP-first.
3. Do you want Milestone 1 API signatures to expose raw `UA_*` structs, or should wrapper DTOs be introduced for common read/write/subscription flows?
Recommendation: expose a thin layer over `UA_*` initially to avoid schema/ABI drift, then wrap higher-level DTOs later if needed.
