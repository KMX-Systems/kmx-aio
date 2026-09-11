# KNXnet/IP

## Implemented Scope

The optional `kmx-aio-knx` product currently provides a tested KNXnet/IP protocol, cEMI, datapoint and
tunnelling foundation:

- KNXnet/IP communication-header encoding and validation
- IPv4 UDP and TCP HPAI encoding and validation, and HPAI decoding of both defined host protocols; which one
  an endpoint serves is its own decision, and a UDP server refuses a TCP HPAI with E_HOST_PROTOCOL_TYPE
- KNXnet/IP over TCP: a frame reassembler; readiness and completion TCP transports; the extended CRI; a
  tunnelling client whose `connect()` opens its connection and whose `send()` and `heartbeat()` only send; and
  `generic_server::serve_connection()` with readiness and completion `tcp_server` accept loops, releasing each
  channel with its connection. Exercised against calimero-server 3.0-M2 with the in-tree client, and against
  xknx 3.20.0 with the in-tree server
- CONNECTIONSTATE_REQUEST and DISCONNECT_REQUEST carrying the sender's control endpoint HPAI - sixteen
  octets, as real peers require
- KNX IP Secure tunnelling client over TCP: the SESSION_REQUEST, SESSION_RESPONSE, SESSION_AUTHENTICATE and
  SESSION_STATUS codecs and MACs; a session that verifies the interface's device authentication code before it
  authenticates, checks every wrapper's session id, MAC and increasing sequence number, and never reuses a key
  or a sequence number across reconnects; and a `tunnelling_client` overload taking `secure_tunnelling_options`,
  which carry the tunnelling credentials, whose `connect()` sends no unencrypted CONNECT when the handshake fails and
  whose `keep_alive()` keeps a session on a quiet bus open. Exercised against calimero-server 3.0-M2
- KNX IP Secure tunnelling server over TCP: `server_config::secure` gives `generic_server` a device authentication
  code and a user table held as derived keys, each user with the tunnel addresses it may be given. On each
  connection the server answers SESSION_REQUEST under a fresh key pair, authenticates the user, opens and seals
  every wrapper under the session key, and serves the tunnel inside as it serves any other; a channel opened in a
  session answers through that session alone. An unencrypted CONNECT_REQUEST is refused with E_CONNECTION_TYPE
  before anything is allocated, other unencrypted traffic but discovery is dropped and counted, and the
  description names tunnelling in a SECURED_SERVICE_FAMILIES block. Sessions are bounded in total and, while
  unauthenticated, per peer address, and a refused SESSION_REQUEST is answered with SESSION_STATUS
  unauthenticated. Exercised against xknx 3.20.0, both ends keyed from one ETS keyring
- SEARCH and DESCRIPTION request/response framing
- typed description information blocks: DEVICE_INFO, SUPP_SVC_FAMILIES, SECURED_SERVICE_FAMILIES,
  IP_CONFIG, IP_CUR_CONFIG, KNX_ADDRESSES and MFR_DATA, with unmodelled types preserved verbatim so a
  decode and re-encode is lossless
- the eight KNXnet/IP service family identifiers, and `dib::family_of` naming the family a service type
  belongs to from its high octet
- server-side service family advertisement: a SEARCH_REQUEST_EXTENDED selecting by service is answered from
  the same list the server advertises, at the version granularity the parameter asks for
- IPv4 and parallel IPv6 SEARCH request/response codec APIs
- `search_all()` collecting every server that answers a multicast SEARCH, each with its source address
- SEARCH_REQUEST_EXTENDED and SEARCH_RESPONSE_EXTENDED, with the four search request parameter blocks
  (programming mode, select by MAC, select by service, request DIBs) and their mandatory flag
- server-side selection on those parameters: a mandatory parameter it cannot match produces silence
- the four tunnelling feature services, with the eight defined feature identifiers and the rules for which
  of them carry a value
- IPv4 and IPv6 SEARCH typed datagram dispatch
- executor-neutral typed discovery client for IPv4 and IPv6 SEARCH responses
- completion real-UDP IPv6 SEARCH socket integration coverage
- routing/multicast configuration validation and executor-neutral indication sender boundary
- ROUTING_INDICATION codec carrying the cEMI frame directly after the KNXnet/IP header, with no
  connection header, per 03/08/05
- ROUTING_BUSY (six-octet block) and ROUTING_LOST_MESSAGE (four-octet block) control codecs with
  typed datagram dispatch, each pinned to a golden wire vector
- readiness and completion transport multicast join/leave runtime socket operations
- routing runtime client API with multicast join/leave and indication send/receive integration
- bounded tunnelling server channel allocator with CONNECT, TUNNELLING, heartbeat and DISCONNECT handling
- server IPv4/IPv6 CONNECT support, negotiated control/data peer routing, sequence enforcement, inactivity
  polling, timeout-tolerant serving, reset, and cancellation-aware continuous serving
- connection status codes carrying the KNXnet/IP values; a server answers a heartbeat or a disconnect for
  a channel it does not hold with E_CONNECTION_ID, and refuses a tunnelling layer it does not serve with
  E_TUNNELLING_LAYER
- gateway composition wrapper combining tunnelling server and routing runtime, either half of which may be KNX IP
  Secure; `server_secured()` and `router_secured()` let a forwarding policy tell them apart
- gateway stop/start lifecycle and continuous serving aliases for both executor pillars
- IPv4 and parallel IPv6 HPAI CONNECT/RESPONSE codec APIs
- IPv4 and IPv6 CONNECT/RESPONSE typed datagram dispatch
- IPv6 client CONNECT and negotiated data-peer routing
- CONNECT and CONNECTIONSTATE control frames, including the individual address the interface assigns
- DISCONNECT control frames
- TUNNELLING_REQUEST and TUNNELLING_ACK framing, with the four-octet KNXnet/IP connection header
- DEVICE_CONFIGURATION_REQUEST and DEVICE_CONFIGURATION_ACK framing
- device management CONNECT_REQUEST/RESPONSE with the two-octet management information block
- cEMI device management services: M_PropRead, M_PropWrite, M_PropInfo and M_Reset
- selectable KNX layer in a tunnelling CONNECT_REQUEST (link layer, raw, bus monitor)
- route-back (NAT) HPAI on both the client and the server, for search, description and connect
- per-service timeouts: tunnelling acknowledgement, connect, connection state and disconnect
- multicast-aware SEARCH that collects every answering server within a window
- DESCRIPTION_REQUEST carrying the requester's control endpoint, and a client `describe()`
- server-side SEARCH and DESCRIPTION answers
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
- an ETS keyring reader, `keyring::load`, for `.knxkeys` exports. It verifies the signature before it decrypts
  anything, and returns backbone, interface, group and device keys and passwords in types that wipe
  themselves. The XML is read by an in-tree reader bounded as in [Operational Limits](#operational-limits).
  It is checked against five ETS 5.7 exports vendored from xknx, and the reader and the loader each have a
  fuzz target
- `keyring::credentials_for`, `keyring::routing_configuration_for` and `keyring::server_configuration_for`, which
  turn a keyring into the derived tunnelling credentials, routing configuration and secure tunnelling server
  configuration a secure endpoint takes, and refuse an all-zero serial number
- KNX IP Secure routing. A `routing::client` constructed with `routing::secure_options` holding a
  `routing::secure_configuration` wraps every frame it sends in a SECURE_WRAPPER under the backbone key, stamped with
  its routing timer. It delivers a received wrapper only once the MAC verifies, the timer falls inside the latency
  window, and the frame is not a repeat; routing traffic that arrives unwrapped is refused and counted. Timers stay aligned through
  TIMER_NOTIFY - synchronisation, timekeeper and follower roles, update notifies for outdated senders - sent by
  `notify_timer()` whenever `next_timer_deadline_ms()` says one is due. It interoperates in both directions with
  xknx 3.20.0 and Calimero 3.0-M2
- SECURE_WRAPPER and TIMER_NOTIFY codecs, sealing and verification, and their `datagram_payload` alternatives
- KNX Data Secure group communication. `data_secure::seal_apdu` and `data_secure::open_apdu` secure and open an
  A_SecureService APDU under either algorithm, pinned to vectors generated from xknx 3.20.0. A `data_secure::context`,
  applied through `tunnelling_client::use_data_secure` or `routing::client::use_data_secure`, secures the telegrams to
  every group with a key and refuses that group's unsecured ones. It verifies the MAC before it believes a sender or a
  sequence number, and reserves outgoing sequence numbers through a `sequence_store` before it sends them.
  `data_secure::configuration_for` builds one from an ETS keyring. Exercised against xknx 3.20.0 over KNX IP Secure
  routing and through a tunnel to a device behind the in-tree server, and against Calimero 3.0-M2's own Data Secure
  over KNX IP Secure routing
- `multicast_group_configuration::loopback`, and multicast sends that leave through the interface the group was
  joined on when `interface_index` names one

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

These details are easy to get wrong in a way no round-trip test can catch, because encoder and decoder
agree with each other while disagreeing with every real interface. Each is pinned by golden byte vectors:

- The connection header of TUNNELLING_REQUEST, TUNNELLING_ACK and the two DEVICE_CONFIGURATION services is
  *structure length, communication channel id, sequence counter, reserved or status* — `04 07 02 00`. The
  leading `0x04` is part of the header: starting at the channel id yields four octets whose every field is
  one early, which no interface accepts and which two ends of this library exchanged happily.
- ROUTING_INDICATION has **no** connection header. The cEMI frame begins immediately after the six-octet
  KNXnet/IP header; there is no channel id and no sequence counter, because routing is connectionless.
- ROUTING_LOST_MESSAGE carries a four-octet block and ROUTING_BUSY a six-octet one, each beginning with its
  own structure length, then a device state octet, then the value.
- DESCRIPTION_REQUEST carries the requester's control endpoint HPAI, so it is fourteen octets, not six. A
  header-only request names nowhere to send the answer.
- A description information block is *structure length, type, data*, and the length counts its own two
  octets. DEVICE_INFO is fixed at 54 octets and IP_CONFIG at 16, so a block of the right type and the wrong
  length is rejected rather than read into the wrong fields. A supported-service-families body is two
  octets per entry, so an odd one is not a list of them.
- A search request parameter block is *structure length, mandatory flag and type, data* — the length counts
  its own two octets, the mandatory flag is the top bit of the type octet, and a block is an even number of
  octets, so a request-DIBs list of odd length is padded. A block whose length is below two would never
  advance a decoder's cursor, and is rejected rather than skipped.
- The tunnelling feature services put the feature identifier and one return-code octet after the connection
  header. That octet is reserved and zero in a get, a set and an info; only a response fills it in. Which
  services carry a value is not free: a get carries none, a set and an info always do, and a response does
  only when it succeeded.

- The connection request information block is *structure length, connection type, KNX layer, reserved* —
  `04 04 02 00`. Swapping the last two asks for KNX layer `0x00`, which no interface accepts.
- The last two octets of the connection response data block are the individual address the interface
  assigned to the tunnel, not a constant. They are decoded into `connect_response_frame::assigned_address`
  and surfaced as `tunnelling_client::assigned_address()`.
- The status octet of CONNECT_RESPONSE, CONNECTIONSTATE_RESPONSE and DISCONNECT_RESPONSE is a KNXnet/IP
  error code, and the codes are not consecutive: E_HOST_PROTOCOL_TYPE is `0x01`, E_CONNECTION_ID `0x21`,
  E_NO_MORE_CONNECTIONS `0x24`, E_KNX_CONNECTION `0x27`. `connect_status` once numbered its values `0x21` to
  `0x26` in declaration order, which this library's client and server agreed on and no other implementation
  did. A server answers a heartbeat or a disconnect for a channel it does not hold with E_CONNECTION_ID.

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

Over a stream transport - KNXnet/IP over TCP - `send()` and `heartbeat()` are the exception. Nothing is
acknowledged over TCP, so both only send: they take turns on an `async_mutex`, and either may run while one task
waits in `receive_cemi()` or `receive_telegram()`, which absorbs the heartbeat's answer. `poll()` reports a
heartbeat whose answer did not come within `connectionstate_timeout_ms`. `connect()` opens the connection and asks
for the tunnel with TCP HPAIs, and `disconnect()`, `shutdown()`, `reset()` and a failed receive close it, so a
reconnect runs over a new connection. The server side is `generic_server::serve_connection()`, run for each
connection by the readiness or completion `tcp_server`.

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
| KNXnet/IP total length field | 65535 | `frame::max_total_length`, the protocol maximum |
| Buffered datagram | 1472 | `frame::max_datagram_size`, one IPv4 UDP payload on Ethernet |
| cEMI message accepted by `send()` | 520 | `cemi::max_message_size`; longer messages cannot be decoded |
| cEMI message accepted by the decoder | 520 | `cemi::max_message_size`, both variable fields at maximum |
| APDU payload octets | 254 | `apdu_payload::max_octets`, the data length field minus one |
| cEMI additional information | 255 | `cemi::max_additional_info_size`, the length field's range |
| Encoded datapoint value | 14 octets | `dpt::payload::capacity`, the width of DPT 16 |
| Tunnelling retries | 2 | `tunnelling_config::max_retries` |
| TUNNELLING_ACK wait | 1 s | `tunnelling_config::ack_timeout_ms` |
| CONNECT / CONNECTIONSTATE / DISCONNECT wait | 10 s each | their own `tunnelling_config` fields |
| Heartbeat interval a supervisor should use | 60 s | `tunnelling_config::heartbeat_interval_ms` |
| Heartbeat failures before teardown | 3 | `tunnelling_config::heartbeat_failure_limit` |
| SEARCH collection window | 3 s | `discovery::client_config::search_timeout_ms` |
| Tunnelling feature value | 16 octets | `tunnelling_feature_value::capacity`; every defined value is 1-2 |
| Search request parameter block | 255 octets | the block's own one-octet structure length |
| ETS keyring document | 1 MiB | `keyring::max_document_size` |
| Keyring XML elements / attributes per element / nesting depth | 4096 / 16 / 8 | `secure::detail::xml_limits` |
| Keyring XML name / decoded attribute value | 64 / 4096 octets | `secure::detail::xml_limits` |
| Signed keyring name or value | 255 octets | the signature stream's one-octet length prefix |
| Secure routing latency tolerance | 1000 ms | `routing::secure_configuration::latency_tolerance_ms`, from the keyring |
| Secure routing duplicate cache | 256 frames | `routing::secure_configuration::duplicate_cache_entries`; more accepted frames than this inside one latency window evict the oldest, whose replay is then accepted |
| Secure routing synchronisation wait | 3.3 s at a 1000 ms tolerance | the longest follower update delay plus twice the tolerance, as in xknx |
| Pending update TIMER_NOTIFYs | 1 | `secure::routing_timer_state`; an outdated frame arriving while one is pending schedules nothing more |
| Secure tunnelling sessions (server) | 16, hard cap 255 | `secure::server_configuration::max_sessions`; room for all of them is reserved when the server is built |
| Unauthenticated sessions per peer address (server) | 2 | `secure::server_configuration::max_unauthenticated_per_peer`; a SESSION_REQUEST past it is answered with SESSION_STATUS unauthenticated |
| Unauthenticated session lifetime (server) | 10 s | `secure::server_configuration::unauthenticated_lifetime_ms`; also how long a connection may carry no session before the server closes it |
| Secure session timeout (server) | 60 s | counted from the last frame received; what the server sends keeps no session alive |
| Sessions per connection (server) | 16 over the connection's life | each session's link lasts as long as its connection, because its channels answer through it |
| Data Secure group keys / senders | 4096 / 4096 | `data_secure::max_table_entries`; a larger configuration is refused |
| Data Secure sequence reservation | 1024 numbers a block | `data_secure::configuration::reservation_block`; each block is recorded through the `sequence_store` before its first number is sent |

The buffered datagram limit is an order of magnitude above what any KNXnet/IP service needs — the longest
tunnelling frame a cEMI message can fill is 530 octets — and is what keeps a 64 KiB array off every
coroutine frame that sends a telegram of a couple of dozen bytes.

`send()` is bounded by what a decoder accepts rather than by what the datagram buffer holds. The buffer
would take 1462 octets, but the cEMI length fields cannot describe a message longer than 520, so anything
above that would encode into a frame every peer must reject.

**Size of a decoded datagram.** `cemi_bytes_storage` holds `cemi::max_message_size` (520 octets), which
makes `knx::datagram` 560 bytes — up from 312 when the storage was sized to `max_l_data_size`. That is the
price of accepting a maximal additional-information block without a heap allocation on the receive path.
The 255-octet block that forces it is legal but not something field devices send; a build that would rather
have the smaller coroutine frame should reject oversized additional information at the decoder instead of
shrinking the storage, which is what reintroduces the overflow.

### Security Statistics

Every secure endpoint counts what it refuses and does, in a `secure::statistics`: `routing::client::secure_counters()`,
`tunnelling_client::secure_counters()`, `generic_server::secure_counters()` and `data_secure::context::counters()`. The
tunnelling client, server and context return copies, because their counters change under a lock; the router's is a
reference to read when nothing is in flight. Every refusal increments a counter (property 10), and none records key
material.

| Counter | Counts |
| :--- | :--- |
| `authentication_failures` | frames whose MAC did not verify, and SESSION_AUTHENTICATE that named an unknown user or a wrong password |
| `replays` | authenticated frames refused as replayed or outside their acceptance window, and reordered Data Secure telegrams |
| `duplicates` | exact repeats of an already accepted routing frame |
| `missing_keys` | frames with no key configured, or from a sender that is not trusted or not allowed for the group |
| `unencrypted_refused` | unencrypted frames refused because the profile or the group requires security |
| `refused_services` | authenticated frames carrying a service that may not be wrapped, or a Data Secure service this build refuses |
| `sessions_opened`, `sessions_closed`, `sessions_timed_out` | secure tunnelling sessions established, closed by either side, and ended by a timeout |
| `timer_notifications_sent`, `timer_adjustments` | TIMER_NOTIFYs a secure router sent, and forward moves of its routing timer |

## Scope Limits

The current implementation does not claim:

- **Point-to-point Data Secure, S-A_Sync and tool access.** Data Secure applies to group communication alone. A
  secured point-to-point telegram, S-A_Sync_Req or S-A_Sync_Res, and a tool-access or system-broadcast telegram are
  refused as unsupported. The tunnelling server and a gateway pass Data Secure APDUs through without opening them. The
  decisions governing the rest are recorded in [Security Decisions](#security-decisions).
- vendor interoperability certification
- IPv6 HPAI integration is implemented for CONNECT, CONNECT_RESPONSE, client data-peer routing, SEARCH
  codec/dispatch, and completion localhost SEARCH socket coverage; real vendor interoperability coverage
  remains a separate follow-up
- vendor multicast routing interoperability beyond loopback/runtime integration
- point-to-point transport connections; a transport control APDU (data length zero) is reported as
  `error::unsupported_service`, so the application-layer property and memory services named in the `apci`
  enumeration cannot be addressed to a single device over a tunnel. Device management reaches a server's
  own interface objects through DEVICE_CONFIGURATION_REQUEST instead
- bus monitor and raw tunnels. The connection codec carries all three KNX layers so a peer's request is
  decoded faithfully, but the cEMI layer decodes only L_Data, so `generic_server` refuses any layer but the
  link layer with E_TUNNELLING_LAYER rather than accepting a connection whose every frame it would then
  reject
- background timers of any kind. `tunnelling_config::heartbeat_interval_ms` states the interval a
  supervisor should use; nothing in the library schedules it
- ETS project parsing; of what ETS exports, only the keyring (`.knxkeys`) is read
- the complete datapoint catalogue; a user can add a `dpt::traits` specialisation for a main type this
  build does not carry without patching the library

Opaque cEMI payload bytes are preserved where the protocol layer does not yet have a named higher-level
schema. Description blocks are now typed; the two whose bodies this build does not model — TUNNELING_INFO
and EXTENDED_DEVICE_INFO — keep their octets so a gateway forwarding a description does not strip them.

## Security Decisions

KNX Secure is being added in stages, and these records are normative for that work. Each says what was
decided and why, so a later change can be checked against the reason rather than against habit. A profile
is listed as supported in [Implemented Scope](#implemented-scope) only once its vectors, its negative tests
and an external interoperability row are green.

### Gates

| Gate | Decision |
| :--- | :--- |
| Specification baseline | KNX System Specifications 03/08/02 "Core", 03/08/04 "Tunnelling", 03/08/09 "KNXnet/IP Security", and the KNX Secure application notes. Protocol constants are cross-checked against xknx 3.20.0 (MIT) and Calimero 3.0-M2. A constant that cannot be tied to a specification clause is marked *interop-derived* where it is defined. |
| Transport profiles | UDP discovery, tunnelling and routing, and tunnelling over TCP, ship today. KNXnet/IP over TCP is a profile of its own, and KNX IP Secure tunnelling is offered over TCP only, until a UDP peer is named. |
| Security release scope | In order: ETS keyring, KNX IP Secure routing, KNXnet/IP over TCP, KNX IP Secure tunnelling client, KNX IP Secure tunnelling server, KNX Data Secure group communication. S-A_Sync, tool access, point-to-point Data Secure and secure device management are out of scope. |
| Crypto boundary | AES-128, SHA-256, PBKDF2-HMAC-SHA256 and X25519 come from the project's TLS backend (OpenSSL 3 or BoringSSL), through EVP calls both provide. Only KNX's composition is written in-tree: its CBC-MAC and CTR construction pads its associated data differently from RFC 3610, so no library CCM or AEAD applies. That composition needs independent vectors and a review by someone who did not write it. Certification is a non-goal. |
| Interoperability | Every released profile names an external peer: xknx for keyring, routing, TCP tunnelling and Data Secure, with Calimero as the second peer. Loopback rows are evidence of the in-tree code only. |
| Operational limits | Checked before allocation: TCP reassembly 1472 octets; keyring document 1 MiB, 4096 elements, 16 attributes per element, depth 8; secure sessions 16 (cap 255), at most 2 unauthenticated per peer address, unauthenticated lifetime 10 s; routing duplicate cache 256 entries. |
| Ownership and shutdown | As for plain KNXnet/IP: endpoints are bound to their transport and are not copyable. Nothing runs a timer; periodic secure work is reported by `poll()` and performed by send-only coroutines. Shared state touched by more than one coroutine is guarded by a lock that is never held across I/O - a mutex for synchronous bookkeeping, an `async_mutex` where sends must keep their order - because executors may resume coroutines on several worker threads. |

### Threat model

The adversary is on the IP network, holds no keys, and can read, inject, replay, delay and drop packets.
Attackers holding a legitimate key, attackers who can read process memory, traffic analysis, and denial of
service beyond bounded resource use are out of scope.

| Profile | Protects | Does not protect |
| :--- | :--- | :--- |
| IP Secure tunnelling | Confidentiality, integrity and freshness of every frame between client and interface; the client authenticates the interface and the interface authenticates the user. Recorded sessions stay confidential if a password leaks later, because every session key comes from fresh X25519 key pairs. | Traffic after the interface forwards it to the bus. Availability. A client that skips device authentication can be intercepted by an active attacker. |
| IP Secure routing | Confidentiality and integrity of multicast traffic among holders of the backbone key; freshness within the latency tolerance; exact replays inside that window. | Any backbone-key holder can forge any router's frames. No forward secrecy. |
| Data Secure (group) | End-to-end integrity, and optionally confidentiality, of group telegrams across routers, gateways and the bus; replay protection per sender. | A group-key holder can claim another sender's address. Addresses stay visible. No forward secrecy. |
| ETS keyring | Keys at rest under the keyring password; tampering, through the signature. | Weak keyring passwords. Keys once loaded into memory. |

### Required properties

1. **No downgrade.** An endpoint configured for a secure profile never sends or accepts that profile's
   unencrypted equivalent. A failed, refused or timed-out handshake leaves it closed, and a
   SECURED_SERVICE_FAMILIES block read from an unauthenticated SEARCH or DESCRIPTION never relaxes the
   requirement.
2. **Verify before state.** No sequence table, timer offset, session state, duplicate cache or counter
   other than a failure statistic changes before the MAC verifies.
3. **Fresh key pairs.** Every session generates a new X25519 key pair; a reconnect is a new session.
4. **No password hashing on network input.** Server configuration holds derived keys only.
5. **Bounded unauthenticated work.** Sessions awaiting authentication are limited globally and per peer
   address; a TIMER_NOTIFY is sent only in response to an authenticated frame, and at most one is pending.
6. **No global crypto hooks.** Randomness and key pairs are injected per object.
7. **Key lifetime.** Keys live in types that wipe themselves, owned by long-lived objects; coroutine frames
   hold references, never copies. Wiping is best effort: copies inside the backend are out of reach.
8. **Unique serial numbers.** Every secure endpoint is configured with a non-zero KNX serial number, and
   there is no library default.
9. **Uniform verification failures.** MACs are compared in constant time, and every MAC failure is reported
   the same way.
10. **Observable failures.** Every rejection increments a counter.

### Decisions

- **The placeholder was removed, not extended.** The provider seam, its `0xFF00` envelope and the invented
  keyring format could not carry KNX Secure: no header, session id, serial number or message tag reached
  the provider, the sequence restarted under a static key on every connect, and control traffic travelled
  in the clear. The conformance gates that pinned the placeholder went with it, since they would have stayed
  green through all of the real work.
- **No build gate.** Secure compiles into `kmx-aio-knx`, because every binary already links the TLS backend,
  and a gate that selects no code misreports coverage (see the comment on `enable_knx` in `source.qbs`).
- **No OpenSSL in public KNX headers.** The crypto adapter lives under `inc/` and `src/`, which keeps the
  OpenSSL/BoringSSL symbol clash out of the KNX API.
- **Password hashing runs when configuration is built.** Every API also accepts the derived 16-octet key.
- **Clocks.** Secure components take a 64-bit millisecond clock: the routing timer is 48 bits wide, and a
  32-bit clock wraps after 49.7 days.
- **Data Secure sequence numbers** are persisted through a caller-supplied store that reserves them in
  blocks; without one, the first value is the number of milliseconds since 2018-01-05T00:00:00Z.
- **Routing duplicates.** Secure routing drops exact repeats of (serial number, message tag, timer value)
  inside the latency window, which the acceptance rule alone would admit.
- **Keyring XML** is read by an in-tree, bounded reader for the subset ETS writes, with DOCTYPE, ENTITY and
  CDATA refused, rather than by a general XML parser.
- **Keyring password padding** is bounded by what follows the eight-octet random prefix, not by the block
  size. The padding is not PKCS#7: ETS repeats nothing but the final count, and ETS 5.7.5 and earlier pad
  every password to two blocks, so real exports carry counts up to 21. A count of zero, or one longer than
  the plaintext, is still refused.
- **Keyring accessors** derive keys when an endpoint is configured, never while a keyring loads, and refuse an
  all-zero serial number instead of supplying a default (property 8). Device authentication is never skipped
  by an accessor; skipping it is a flag the caller sets by name.
- **Secure routing timer.** The acceptance rule, TIMER_NOTIFY scheduling and synchronisation follow xknx 3.20.0,
  the interoperability peer, event by event (AN159 events E1 to E11). Wrappers arriving before the timer has
  synchronised are dropped, as xknx drops them, so `routing::client::timer_synchronised()` tells an application
  when a telegram it sends can be answered.
- **A router's own traffic.** A secure router remembers each wrapper it sends in its duplicate cache as well as
  in the reflection history, so its own traffic reflected back by the group is never delivered, however many sends
  ago it left. Its synchronisation request is recorded the same way, which is why its own reflected request can
  never be taken for the answer to it.
- **Routing carries L_Data.ind.** The routing client sends the cEMI it is given. A peer drops an L_Data.req on
  the routing group, as xknx does, so applications put L_Data.ind frames on it.
- **Refused services are counted.** `secure::statistics::refused_services` counts authenticated wrappers carrying
  a service routing does not take in a wrapper, so that such a refusal is observable too (property 10).
- **Secure server refusals.** An unencrypted CONNECT_REQUEST to a secure server is refused with E_CONNECTION_TYPE
  (0x22), the code calimero-server 3.0-M2 answers with, and nothing is allocated for it. A SESSION_REQUEST past a
  session limit is answered in the clear with SESSION_STATUS unauthenticated, since no key exists yet to seal it
  under, so a client fails at once instead of waiting out its handshake. An authentication that does not verify is
  answered under its session, which then ends.
- **Server session timeout.** A server session times out 60 s after the last frame the server received, not the last
  one it sent: a client that has gone away must be noticed however much the bus has to tell it. A client's
  keep-alive, due 50 s after its own last wrapper, keeps a quiet session inside that window.
- **Tunnel addresses per user.** A secure channel is given one of its user's tunnel addresses. An address that is not
  the user's is refused with E_AUTHORISATION_ERROR, one in use with E_CONNECTION_IN_USE, and a user with none free
  with E_NO_MORE_UNIQUE_CONNECTIONS.
- **Data Secure checks in the order P2 asks for.** The MAC is verified before the sender table is consulted, and a
  sender's last sequence number changes only once it has. xknx looks the sequence number up first. Every genuine
  telegram gets the same verdict either way; a forged one is counted as an authentication failure rather than a replay.
- **Data Secure authentication only** carries the APDU and its 4-octet MAC in the clear: the MAC is not CTR-encrypted.
  The vectors generated from xknx 3.20.0 pin this.
- **Allowed senders.** A group the local interface lists senders for takes telegrams from those senders alone. A group
  it lists none for takes them from any sender the keyring names. A confirmation of the endpoint's own telegram is
  opened without consulting the sender table.
- **Frame type after securing.** A secured or opened telegram is marked standard or extended by its new length, as
  xknx marks it. The extended frame format field in control field 2, which the MAC binds, is left as it was.

## Interoperability Matrix

Interoperability evidence is now maintained as data in
`documentation/features/knx/interoperability/evidence.tsv` and rendered to
`documentation/features/knx/interoperability/matrix.md`.

Use the helper below to validate required profile coverage and regenerate the published matrix document.
`verify-release` additionally requires every required profile to have a passing row, or only skipped rows that say why;
the release gate runs it:

```bash
bash script/feature/knx/interoperability-matrix.sh verify
bash script/feature/knx/interoperability-matrix.sh verify-release
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
`[session]`, `[client]`, `[server]`, `[routing]`, `[tcp]`, `[dib]`, `[secure]`, `[crypto]`, `[keyring]`, `[data_secure]`,
`[concurrency]` and `[interop]`. Unit-style client tests use an injected fake datagram transport; socket-tagged
integration tests additionally exercise real localhost UDP and TCP endpoints and executor-backed deadline expiry.

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

The keyring tests read the five ETS exports under `documentation/features/knx/conformance/keyrings/`; the
`PROVENANCE.md` beside them records where they came from and their passwords. Five libFuzzer targets live under
`source/fuzz/knx/`:

- the XML reader and the keyring loader, seeded from those keyrings;
- the datagram decoder, which reaches every KNXnet/IP codec and opens, verifies and checks what decodes under a fixed key;
- the KNXnet/IP over TCP frame reassembler;
- the Data Secure codec and context.

The last three are seeded from the generated vectors by `source/fuzz/knx/seed_corpus.py`. The script below builds the
targets with clang under AddressSanitizer and UndefinedBehaviorSanitizer, and keeps each corpus under
`output/fuzz/knx/`, so a later run resumes where an earlier one stopped:

```bash
bash script/feature/knx/run-fuzz.sh                      # one CPU-hour per target
KMX_FUZZ_SECONDS=60 bash script/feature/knx/run-fuzz.sh  # a smoke run
bash script/feature/knx/run-fuzz.sh datagram data_secure # the targets named
```

The KNX Secure suites - `[knx][secure]`, `[knx][keyring]`, `[knx][tcp]` and `[knx][data_secure]` - build and run on
either TLS backend with the script below. It fails when no test binary was built, which a failed BoringSSL bootstrap
would otherwise hide behind a successful exit:

```bash
bash script/feature/knx/run-secure-suites.sh openssl
bash script/feature/knx/run-secure-suites.sh boringssl
```

The pinned interoperability peers - xknx 3.20.0, a JDK 21 and Calimero 3.0-M2 - are installed under `output/interop/` by
`bash script/feature/knx/interop/install-peers.sh`. What the security review checks, property by property, is in
[knx/security-review.md](knx/security-review.md).

Secure routing interoperability runs the in-tree router against a pinned peer on the loopback interface, one
telegram each way, with both sides keyed from the vendored `keyring.knxkeys`. The Catch2 case behind it,
`[knx][secure][routing][interop]`, skips unless the script has started a peer:

```bash
bash script/feature/knx/interop/run-secure-routing-interop.sh xknx      # xknx 3.20.0 from output/interop/xknx-venv
bash script/feature/knx/interop/run-secure-routing-interop.sh calimero  # Calimero 3.0-M2 with JDK 21
```

KNX IP Secure tunnelling runs the same way, one direction per peer. The client case,
`[knx][secure][tunnelling][interop]`, opens a session to calimero-server. The server case,
`[knx][secure][server][interop]`, serves xknx, with both ends keyed from `testcase.knxkeys`. xknx reads a server's
description over UDP before it opens a secure tunnel whose credentials come from a keyring, so the server case
answers description on its TCP port number as well:

```bash
bash script/feature/knx/interop/run-secure-tunnelling-interop.sh calimero  # in-tree client, calimero-server 3.0-M2
bash script/feature/knx/interop/run-secure-tunnelling-interop.sh xknx      # in-tree server, xknx 3.20.0
```

KNX Data Secure runs against xknx in two settings, keyed from `keyring.knxkeys`: over KNX IP Secure routing between two
routers, and through a tunnel to a device behind the in-tree server. Over routing it also runs against Calimero, whose
`SecureApplicationLayer` keys itself with Calimero's own keyring reader. The vectors its tests pin come from
`script/feature/knx/secure-vectors/generate.py`, which writes `data-secure-vectors.tsv` beside the routing vectors:

```bash
bash script/feature/knx/interop/run-data-secure-interop.sh routing           # xknx 3.20.0
bash script/feature/knx/interop/run-data-secure-interop.sh routing calimero  # Calimero 3.0-M2, on port 3671
bash script/feature/knx/interop/run-data-secure-interop.sh tunnel            # xknx 3.20.0
```

The SECURE_WRAPPER and TIMER_NOTIFY vectors in `documentation/features/knx/conformance/secure-routing-vectors.tsv`
come out of xknx's own code paths, through `script/feature/knx/secure-vectors/generate.py`. The concurrency case,
`[knx][secure][routing][concurrency]`, receives on one router while another task notifies and sends on it, on a
two-thread executor; it is the one the ThreadSanitizer run is for.

On 2026-09-10 each of the five fuzz targets ran for one CPU-hour without a finding:

| Target | Executions |
| :--- | ---: |
| XML reader | 80.2 million |
| keyring loader | 14.7 million |
| datagram decoder | 1.66 billion |
| frame reassembler | 934 million |
| Data Secure | 765 million |

The same day the KNX unit and integration suites ran clean under ThreadSanitizer, and under AddressSanitizer with
UndefinedBehaviorSanitizer. Those suites include the secure, keyring, TCP and Data Secure ones. The KNX Secure suites also
passed on the BoringSSL backend.

The latest KNX-focused run of the readiness-and-completion binary, on OpenSSL, reports 3,108 assertions in 339 unit
test cases - 7 of them skipped, the interoperability cases without a peer - and 889 assertions in 128 integration test
cases. The BoringSSL build is checked by `run-secure-suites.sh boringssl` and by the `knx-secure` CI job. The release gate
additionally runs vendor bundle validation; the focused server, routing, keyring, secure, TCP and Data Secure suites; and
the full regression suite.

The `ci-knx` workflow also runs the KNX unit **and integration** suites under AddressSanitizer and
UndefinedBehaviorSanitizer. Only the unit half used to be instrumented, which is why a remotely reachable
out-of-bounds write in the tunnelling decoder and an out-of-bounds read in the client constructor both
survived a green suite. Beside that job, `knx-secure` runs the KNX Secure suites on OpenSSL and on BoringSSL,
`knx-thread-sanitizer` runs the KNX suites under ThreadSanitizer, and `knx-interop` - on demand and nightly - installs the
pinned peers and runs every interoperability script above.

To exercise both supported KNX executor configurations in one pass:

```bash
bash script/feature/knx/run-matrix-tests.sh
```

The release gate builds both executor pillars with the same qbs profile the other build scripts use, then
runs the KNX suites - the keyring, secure, TCP and Data Secure ones by name - and the full suite, and checks the
interoperability matrix with `verify-release`:

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

That gate is the complete in-tree verification path. Every KNX Secure row of the interoperability matrix rests on an
exchange with an external implementation - xknx or Calimero - on the loopback interface. Each interop script writes the
peer's log to `output/interop/captures/`, and the `knx-interop` CI job uploads that folder as the `knx-interop-captures`
artifact. The logs are not versioned, because the pinned peers and the scripts can regenerate them. Captures from vendor
devices remain required before any certification claim.

The KNX wire vectors in `inc_dep/kmx/aio/knx/detail/codec_vectors.hpp` are checked at compile time, so a
wire-format regression fails the build before any release gate runs.
