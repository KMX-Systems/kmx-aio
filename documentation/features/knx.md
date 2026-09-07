# KNXnet/IP

## Implemented Scope

The optional `kmx-aio-knx` product currently provides a tested KNXnet/IP protocol, cEMI, datapoint and
tunnelling foundation:

- KNXnet/IP communication-header encoding and validation
- IPv4 UDP HPAI encoding and validation
- SEARCH and DESCRIPTION request/response framing with opaque DIB preservation
- IPv4 and parallel IPv6 SEARCH request/response codec APIs
- IPv4 and IPv6 SEARCH typed datagram dispatch
- executor-neutral typed discovery client for IPv4 and IPv6 SEARCH responses
- completion real-UDP IPv6 SEARCH socket integration coverage
- KNX Secure profile/replay-policy configuration boundary with injectable crypto provider interface
- provider-backed KNX Secure wire-envelope packet codec and typed datagram dispatch
- secure packet protect/unprotect helpers with optional replay-window enforcement
- optional Secure configuration/provider validation at the tunnelling client boundary
- routing/multicast configuration validation and executor-neutral indication sender boundary
- routing indication cEMI envelope codec with strict channel/reserved/length validation
- routing BUSY and LOST_MESSAGE control codecs with typed datagram dispatch
- readiness and completion transport multicast join/leave runtime socket operations
- routing runtime client API with multicast join/leave and indication send/receive integration
- bounded tunnelling server channel allocator with CONNECT, TUNNELLING, heartbeat and DISCONNECT handling
- server IPv4/IPv6 CONNECT support, negotiated control/data peer routing, sequence enforcement, inactivity
  polling, timeout-tolerant serving, reset, and cancellation-aware continuous serving
- gateway composition wrapper combining tunnelling server and routing runtime
- gateway stop/start lifecycle and continuous serving aliases for both executor pillars
- bounded keyring parser with selected-record lookup and encrypted-key decryptor seam
- KNX Secure golden envelope vectors with strict malformed-frame reject coverage
- data-driven KNX Secure conformance vector ingestion for external standards bundles
- data-driven `.knxkeys` conformance bundle ingestion with password-id keyed decryptor selection
- deterministic provider contract coverage for sequence-bound protect/unprotect behavior
- wrap-safe replay-window state primitive with duplicate/stale sequence rejection
- explicit `protect_payload()` / `unprotect_payload()` client transform boundary
- secure-profile client data path wraps outgoing tunnelling packets and unwraps incoming data packets
- IPv4 and parallel IPv6 HPAI CONNECT/RESPONSE codec APIs
- IPv4 and IPv6 CONNECT/RESPONSE typed datagram dispatch
- IPv6 client CONNECT and negotiated data-peer routing
- CONNECT and CONNECTIONSTATE control frames, including the individual address the interface assigns
- DISCONNECT control frames
- TUNNELLING_REQUEST and TUNNELLING_ACK framing
- Typed datagram dispatch and response encoding
- KNX individual and group address value types, with text parsing and formatting in all three group styles
- cEMI L_Data encoding and decoding: control fields, addressing, TPCI/APCI, compact and extended APDUs,
  additional information blocks
- Datapoint types 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 20 and 232, plus the scaled
  sub types 5.001 and 5.003
- A deterministic tunnelling session state machine with:
  - sequence allocation and wraparound
  - ACK matching
  - retransmission packet retention
  - heartbeat failure escalation
  - inactivity timeout handling
  - graceful disconnect
  - reset and shutdown semantics
- Executor-neutral coroutine transport contract and injected tunnelling client boundary
- Readiness/completion public aliases for the shared client type
- Readiness/completion public aliases for the shared server and gateway types

The feature is enabled with `project.enable_knx:true` and is excluded from default builds.

## The Pure Layer and Its Error Type

`address.hpp`, `cemi.hpp` and `dpt.hpp` are `constexpr`, allocation-free and free of I/O, and they report
failures as `knx::error` rather than as `std::error_code`.

That split is deliberate and load-bearing, not a style slip. `make_error_code` reaches a function-local
static `std::error_category`, which is not a constant expression, and `std::error_code` is not a literal
type — so a codec that reported failures that way could not run during translation at all. Reporting the
enumeration instead is what lets `inc/kmx/aio/knx/detail/codec_vectors.hpp` encode and decode captured wire
bytes inside `static_assert`, so a change to the wire format stops the build rather than shipping a library
that talks to nothing.

A well-meaning edit that "restores consistency" by returning `std::error_code` from these headers would
silently un-`constexpr` the whole codec, and nothing would fail to compile until the assertions were
deleted along with it.

The KNXnet/IP framing layer (`frame`, `connection`, `discovery`, `datagram`) and the session still speak
`std::error_code`, and convert at exactly one point: `frame::decode_cemi`. Public coroutine APIs return
`std::error_code` because their other failure source is the transport.

## Wire Format Notes

Two details are easy to get wrong in a way no round-trip test can catch, because encoder and decoder agree
with each other while disagreeing with every real interface. Both are pinned by golden byte vectors in
`connection_test.cpp`:

- The connection request information block is *structure length, connection type, KNX layer, reserved* —
  `04 04 02 00`. Swapping the last two asks for KNX layer `0x00`, which no interface accepts.
- The last two octets of the connection response data block are the individual address the interface
  assigned to the tunnel, not a constant. They are decoded into `connect_response_frame::assigned_address`
  and surfaced as `tunnelling_client::assigned_address()`.

cEMI message codes are the real ones — `0x11` request, `0x2E` confirmation, `0x29` indication — and the
smallest well-formed L_Data message is eleven octets, not eight.

## Current Client Boundary

`tunnelling_client` owns the KNX session state machine and receives a `knx::datagram_transport`
implementation. The transport owns the executor-bound UDP endpoint. This keeps readiness and completion I/O
separate from protocol framing and state transitions.

The current client slice covers:

- connect request/response
- one tunnelling request/ACK exchange
- disconnect request/response
- timeout-triggered connect and tunnelling retries
- readiness and completion UDP adapter contracts
- completion and readiness real-UDP loopback integration tests
- negotiated control/data endpoint routing after CONNECT_RESPONSE
- peer-address filtering before protocol dispatch
- serialized public client operations; conflicting concurrent operations return `error::send_queue_full`
- duplicate incoming indications are acknowledged without duplicate application delivery
- explicit `tunnelling_client::poll()` inactivity supervision
- absolute receive-deadline propagation through the transport extension point
- bounded inline storage for decoded cEMI payloads
- configured IPv4/IPv6 endpoint length validation before I/O
- family-aware IPv4 and IPv6 peer comparison, including IPv6 scope and flow fields
- service-specific peer binding before session dispatch
- atomic public-operation acquisition for cross-thread entry
- configured-peer family and length validation before sending
- disconnect responses use the same peer validation as application/control receives
- owning `receive_datagram()`, `receive_cemi()` and `receive_telegram()` application APIs
- typed group services: `write_group_value()`, `read_group_value()`, `respond_group_value()`

The routing client exposes `receive_event()` for indications, BUSY controls, and LOST_MESSAGE controls.
BUSY events establish a clock-aware send backoff; locally reflected indications are suppressed and counted.
The tunnelling server uses separate control and negotiated data peers, rejects unusable HPAI metadata before
allocation, re-ACKs duplicate indications without redelivery, rejects stale sequences, and releases inactive
channels through `poll()` and normal `serve_once()` processing.

The client retries only when the injected transport reports `error::timeout`. Protocol errors and
non-timeout transport errors are returned immediately. The session retains the encoded packet and sequence
number across retries, so retransmission is byte-identical.

The injected transport remains responsible for socket cancellation and receive-loop ownership. The
readiness and completion UDP adapters enforce absolute receive deadlines through their executor backends.
The client validates the transport-reported peer before protocol dispatch and
routes control traffic through the configured peer and tunnelling traffic through the negotiated data
endpoint. Public client operations are serialized because the session and receive buffer are shared; the
client does not yet run an independently owned background receive or heartbeat task. Gate acquisition is
atomic, so concurrent callers fail deterministically with `error::send_queue_full`; callers must still
quiesce outstanding operations before invoking `shutdown()` or `reset()`.

This is an intentional single-operation ownership model: one public operation owns the transport receive
path and shared session at a time. Applications that need simultaneous application delivery and protocol
supervision should add an external coordinator or move to a future dispatcher API; they must not start
multiple KNX public operations against one client concurrently.

`reset()` and a subsequent `connect()` discard the previous negotiated data endpoint and require a fresh
CONNECT response. This prevents a reconnect from sending tunnelling traffic to a stale interface port.

Incoming control services must arrive from the configured control peer, while tunnelling requests and ACKs
must arrive from the negotiated data peer. A packet from the wrong endpoint is rejected before it can
change session state or reach the application.

The concrete client now validates the transport-reported peer address against the configured peer before
decoding application or control datagrams. Invalid peer metadata, malformed packets, and unsupported
application services are returned as errors without exposing payloads to the caller.

`receive_cemi()` skips valid internal ACK and heartbeat-response datagrams, processes heartbeat responses
through the session supervisor, and waits for the next application tunnelling indication. A valid
indication is ACKed before its owning cEMI bytes are returned. Cross-channel indications, malformed
packets, and unsupported services are rejected and are never acknowledged or delivered. Incoming
indications must progress in modulo-256 order; an exact duplicate of the last accepted indication is
ACKed again but is not delivered a second time, while stale or out-of-order indications are rejected.
Delayed ACKs for another channel or sequence are ignored while an active send or heartbeat waits for its
match.
`receive_telegram()` decodes those octets into a `knx::telegram`, which owns them so the decoded frame's
payload view stays valid.

Decoded tunnelling requests keep their cEMI octets in bounded inline storage sized to the maximum L_Data
message. The public owning receive APIs copy once into their returned vector; codec dispatch and duplicate
handling do not allocate per datagram.

The client does not create a background timer task. Supervisors should call `poll()` periodically; it
returns `error::inactivity_timeout` and closes the session once no accepted traffic has arrived within
`tunnelling_config::inactivity_timeout_ms`.

Secure payload transforms remain available as explicit client APIs. When a secure profile is configured,
the client now applies secure wire wrapping to outgoing tunnelling packets and unwraps incoming data-path
packets before session dispatch, with replay-window enforcement through the secure configuration policy.

Each connect and tunnelling request computes an absolute ACK deadline from the injected monotonic clock
and `ack_timeout_ms`. The transport contract exposes `receive_until()` for executor adapters that can
enforce that deadline. The readiness UDP adapter enforces it with executor-managed per-waiter epoll
deadlines, while the completion adapter uses a linked `recvmsg`/io_uring timeout pair; both map expiry to
`error::timeout`. The compatibility default still delegates to `receive()` for transports without native
deadline support. The session API continues to accept already-absolute deadlines, which keeps its
deterministic state-machine tests independent of wall-clock policy.

Disconnect response waits use the same deadline-aware receive hook and recompute the deadline for each
bounded retry, so teardown does not bypass transport-specific timeout enforcement.

The client validates configured sockaddr lengths for known address families before sending. IPv4 and IPv6
addresses must provide their complete native sockaddr structure; the existing unspecified-address wildcard
is retained for executor-neutral test transports.

Client `connect()` also rejects zero-port or non-UDP HPAI metadata before creating a handshake packet. The
lower-level connection codec remains usable with synthetic aggregate frames for deterministic state-machine
tests; transport usability is enforced at the I/O boundary.

After client shutdown, `receive_datagram()` and `receive_cemi()` fail immediately with `error::shutdown`
without invoking the injected transport. `reset()` is the explicit reuse operation after outstanding
coroutine work has been quiesced.

The client exposes `connected()`, `closing()`, and `closed()` predicates for supervisors that should not
depend on raw enum comparisons.

Zero-length datagrams and datagrams larger than the client receive buffer are rejected before decoding.
Configured peer lengths must fit `sockaddr_storage`, and the received family and length must match the
configured peer.

Outgoing operations reject empty cEMI payloads, unsupported HPAI protocols, invalid configured peer
lengths, and partial transport writes before treating a packet as successfully transmitted.

Non-timeout transport errors preserve their original `std::error_code` value and category across client
connect, send, heartbeat, disconnect, and receive APIs. The client intentionally maps partial writes and
peer validation failures to `connection_failed`; timeout is consumed only by the bounded retry policies.

A failed or invalid `CONNECT_RESPONSE` returns an active connection attempt to `idle` and clears retained
handshake bytes, allowing an explicit retry or reset. Application cEMI payloads returned by
`receive_cemi()` are owning vectors and remain valid after the next transport receive.

## Operational Limits

| Limit | Value | Where |
| :--- | :--- | :--- |
| KNXnet/IP total length field | 65535 | `frame::max_frame_size`, the protocol maximum |
| Buffered datagram | 1472 | `frame::max_datagram_size`, one IPv4 UDP payload on Ethernet |
| cEMI message accepted by `send()` | 1462 | buffered datagram minus the KNXnet/IP and connection headers |
| APDU payload octets | 254 | `apdu_payload::max_octets`, the data length field minus one |
| Encoded datapoint value | 14 octets | `dpt::payload::capacity`, the width of DPT 16 |
| Tunnelling retries | 2 | `tunnelling_config::max_retries` |
| Heartbeat failures before teardown | 3 | `tunnelling_config::heartbeat_failure_limit` |

The buffered datagram limit is an order of magnitude above what any KNXnet/IP service needs — the longest
tunnelling frame a cEMI message can fill is about 530 octets — and is what keeps a 64 KiB array off every
coroutine frame that sends a telegram of a couple of dozen bytes.

## Scope Limits

The current implementation does not claim:

- vendor interoperability certification
- IPv6 HPAI integration is implemented for CONNECT, CONNECT_RESPONSE, client data-peer routing, SEARCH
  codec/dispatch, and completion localhost SEARCH socket coverage; real vendor interoperability coverage
  remains a separate follow-up
- external certification evidence for vendor KNX Secure cryptographic interoperability
- vendor multicast routing interoperability beyond loopback/runtime integration
- point-to-point transport connections; a transport control APDU (data length zero) is reported as
  `error::unsupported_service` rather than decoded
- ETS project parsing
- the complete datapoint catalogue; a user can add a `dpt::traits` specialisation for a main type this
  build does not carry without patching the library

Opaque DIB and cEMI payload bytes are preserved where the protocol layer does not yet have a named
higher-level schema.

## Interoperability Matrix

Interoperability evidence is now maintained as data in
`documentation/features/knx/interoperability/evidence.tsv` and rendered to
`documentation/features/knx/interoperability/matrix.md`.

Use the helper below to validate required profile coverage and regenerate the published matrix document:

```bash
bash script/feature/knx/interoperability-matrix.sh verify
bash script/feature/knx/interoperability-matrix.sh render
```

Vendor interoperability campaign rows can be validated (or imported) from
`documentation/features/knx/interoperability/vendor-results.tsv`:

```bash
bash script/feature/knx/run-vendor-interoperability.sh --validate
# then when vendor capture files are present:
bash script/feature/knx/run-vendor-interoperability.sh --apply
```

## Verification

KNX unit and integration-style tests use Catch2 tags `[knx]`, `[knx][integration]`, and service-specific
tags `[address]`, `[cemi]`, `[dpt]`, `[codec]`, `[frame]`, `[connection]`, `[discovery]`, `[datagram]`,
`[session]`, `[client]`. Unit-style client tests use an injected fake datagram transport; socket-tagged
integration tests additionally exercise real localhost UDP endpoints and executor-backed deadline expiry.

```bash
cd source
qbs resolve -d ../output/knx -f source.qbs config:debug project.enable_knx:true project.enable_readiness:true
qbs build -d ../output/knx -f source.qbs config:debug -j"$(nproc)"
cd ..
KMX_BUILD_ROOT=output/knx bash script/feature/knx/run-unit-tests.sh
KMX_BUILD_ROOT=output/knx bash script/feature/knx/run-integration-tests.sh
```

For the minimal feature matrix, disable readiness explicitly and keep the output in its own build root:

```bash
qbs build -d output/debug-knx-minimal -f source/source.qbs config:debug \
  project.enable_knx:true project.enable_readiness:false
KMX_BUILD_ROOT=output/debug-knx-minimal bash script/feature/knx/run-unit-tests.sh
KMX_BUILD_ROOT=output/debug-knx-minimal bash script/feature/knx/run-integration-tests.sh
```

The current secure/keyring-gated readiness-enabled test binary reports 9,866 passing assertions in 560 tests;
the latest KNX-focused run reports 1,388 assertions in 274 tests. The release-gate workflow additionally runs
secure conformance (21 assertions / 1 test), keyring conformance (23 assertions / 1 test), vendor bundle
validation (6 imported rows), and the focused server and routing suites before the full regression suite.

To exercise both supported KNX executor configurations in one pass:

```bash
bash script/feature/knx/run-matrix-tests.sh
```

The secure/keyring release gate builds both executor pillars with
`project.enable_knx_secure:true` and `project.enable_knx_keyring:true`, then runs the KNX and full suites:

```bash
bash script/feature/knx/run-release-gates.sh
```

To append new interoperability evidence rows and re-render in one step:

```bash
bash script/feature/knx/interoperability-matrix.sh record \
  --profile "SEARCH / DESCRIPTION" \
  --peer "vendor-x fw-1.2.3" \
  --transport "UDP 3671" \
  --result passing \
  --capture "captures/knx/vendor-x-search.pcapng" \
  --notes "stable over 100 discovery exchanges"
```

Secure and keyring conformance bundles can be run directly:

```bash
bash script/feature/knx/run-secure-conformance.sh
bash script/feature/knx/run-keyring-conformance.sh
```

For external standards or lab bundles, override input paths with environment variables:

```bash
KMX_KNX_SECURE_VECTOR_FILE=/path/to/official-secure-vectors.tsv \
  bash script/feature/knx/run-secure-conformance.sh

KMX_KNX_KEYRING_CONFORMANCE_DIR=/path/to/keyring-bundle \
KMX_KNX_KEYRING_CASES_FILE=/path/to/keyring-bundle/cases.tsv \
KMX_KNX_KEYRING_PASSWORDS_FILE=/path/to/keyring-bundle/passwords.tsv \
  bash script/feature/knx/run-keyring-conformance.sh
```

That gate is the complete in-tree verification path. The current interoperability matrix is closed with
loopback-backed evidence rows; external vendor captures remain required before certification claims.

The KNX wire vectors in `source/library-test/inc_dep/kmx/aio/knx/detail/codec_vectors.hpp` and
`source/library-test/inc_dep/kmx/aio/knx/detail/secure_vectors.hpp` are checked by tests, so a
wire-format regression fails before release gates complete.
