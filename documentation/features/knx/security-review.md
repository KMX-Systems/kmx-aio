# KNX Secure Security Review

This is the review checklist the KNX Secure release plan asks for before a secure profile ships. It maps each required
property (P1–P10, recorded under [Security Decisions](../knx.md#security-decisions)) to the code that enforces it and
the tests that show it. It also lists what a reviewer has to check by reading, because no test can show it.

**Status: prepared, awaiting independent sign-off.** The author of the code prepared this checklist. The plan requires
sign-off by a reviewer who did not write the code. Until such a reviewer has worked through every section and signed
the table at the end, no profile counts as reviewed.

## Profiles in scope

| Profile | Entry points | External evidence |
| :--- | :--- | :--- |
| ETS keyring | `keyring::load`, `credentials_for`, `routing_configuration_for`, `server_configuration_for`, `data_secure::configuration_for` | xknx 3.20.0 fixtures |
| KNX IP Secure routing | `routing::client` built with a `routing::secure_configuration` | xknx 3.20.0, Calimero 3.0-M2 |
| KNX IP Secure tunnelling, client | `tunnelling_client` built with `secure::tunnelling_credentials` | calimero-server 3.0-M2 |
| KNX IP Secure tunnelling, server | `generic_server` built with `server_config::secure` | xknx 3.20.0 |
| KNX Data Secure, group | `data_secure::context`, through `tunnelling_client::use_data_secure` and `routing::client::use_data_secure` | xknx 3.20.0, over routing and through a tunnel; Calimero 3.0-M2, over routing |

The captures behind the external evidence are in [interoperability/matrix.md](interoperability/matrix.md).

## Required properties

| Property | Where it is enforced | What shows it |
| :--- | :--- | :--- |
| **P1 No downgrade** | Client: `secure::tunnel_transport::open` closes the connection when a handshake fails, and its receive refuses an unwrapped tunnel frame. Server: `generic_server::refuses_in_clear` refuses an unencrypted CONNECT_REQUEST with E_CONNECTION_TYPE before anything is allocated, and drops every other unencrypted frame except discovery. Routing: an unwrapped routing frame to a secure router is refused. Data Secure: `context::open_frame` refuses an unsecured telegram to a keyed group, and `secure_frame` refuses to send a telegram to such a group that it cannot secure. | `[knx][secure][tunnelling]` (no downgrade after a forged response, a refused user or a silent server); "knx completion secure tcp server refuses an unencrypted tunnel and allocates nothing for it"; `[knx][secure][routing]`; "knx data secure refuses an unencrypted telegram to a secured group". |
| **P2 Verify before state** | `secure::client_session::open`, `secure::server_session_table::open`, the routing unwrap and `data_secure::context::open_secured` each check in the same order: session or key, then MAC, then sequence number or timer, then service. The sequence table, timer offset, duplicate cache and session state change only after the MAC verifies. | "knx secure server session table checks a wrapper's connection, MAC and sequence before what it carries"; "knx data secure refuses stale sequence numbers, and a forged MAC changes nothing"; the client session and routing timer suites. |
| **P3 Fresh key pairs** | `client_session::begin` and `server_session_table::on_session_request` draw a new X25519 key pair for every session. A reconnect begins a new session. | "reconnect through a fresh key" in `[knx][secure][tunnelling]`; "knx secure server session table gives every live session its own id and its own key pair". |
| **P4 No password hashing on network input** | `secure::server_configuration` and `secure::tunnelling_credentials` hold derived keys only. The PBKDF2 derivations are called by the keyring accessors and by configuring code, never by a frame handler. | Reviewer check: `grep -rn "derive_user_password_key\|derive_device_authentication_code\|derive_keyring_password_hash" source/library/src` finds only the keyring accessors and the key module. |
| **P5 Bounded unauthenticated work** | `server_session_table`: at most `max_sessions` sessions (hard cap 255), `max_unauthenticated_per_peer` unauthenticated sessions per address, `unauthenticated_lifetime_ms`; room reserved up front. The server closes a connection that carries no session for that lifetime, and allows 16 sessions per connection. The routing timer sends a TIMER_NOTIFY only in response to an authenticated frame, with at most one pending. | "knx secure server session table limits unauthenticated sessions per peer address and sessions in total"; "... reaps a handshake left unfinished ..."; "knx readiness secure tcp server turns away a third handshake from one address"; "... closes a connection that never opens a session"; the routing timer suite. |
| **P6 No global crypto hooks** | Randomness comes from an `entropy_source` injected into each client, server and router, defaulting to `secure::system_entropy()`. The backend's `RAND_bytes` is called in one place, `secure/crypto.cpp`. | Reviewer check: `grep -rn "RAND_bytes\|random_device\|rand()" source/library` finds that one call. Tests inject fixed-key sources throughout. |
| **P7 Key lifetime** | Keys live in `secret_key` and `secret_string`, which wipe themselves (`secure/key.cpp`). Buffers that held plaintext are wiped on failure in `secure/ccm.cpp`, `secure/wrapper.cpp`, `secure/client_session.cpp`, `secure/server_session_table.cpp`, `secure/keyring_format.cpp` and `data_secure.cpp`. Keys stay in long-lived objects - the client session, the server session table, the routing state, the Data Secure context - and coroutines reach them through references. | Reviewer check: no library coroutine takes a `secret_key`, `tunnelling_credentials` or `server_configuration` by value (`grep -rn "task<" source/library/api source/library/inc` against the parameter lists). Wiping is best effort: copies inside the backend are out of reach. |
| **P8 Unique serial numbers** | `keyring::credentials_for`, `routing_configuration_for` and `server_configuration_for` refuse an all-zero serial number; `client_session::begin`, the secure routing client's start, and the server session table refuse to run without one. There is no library default. | "knx keyring refuses credentials it cannot build"; "knx keyring builds a secure tunnelling server's configuration ..."; "knx secure server session table refuses a session under an all-zero serial number". |
| **P9 Uniform verification failures** | MACs are compared with `secure::detail::constant_time_equal` - in `ccm.cpp` for wrappers and TIMER_NOTIFY, `session.cpp` for the handshake, `data_secure.cpp` for S-A_Data, and `keyring_format.cpp` for the keyring signature. Every MAC failure is reported as `secure_authentication_failed` (the keyring's as `keyring_signature_invalid`), whichever field was altered. | Codec suites assert the one error for a MAC, a session id and a sequence number altered in turn. Reviewer check: no early return compares MAC octets. |
| **P10 Observable failures** | Every refusal increments a `secure::statistics` counter: `authentication_failures`, `replays`, `duplicates`, `missing_keys`, `unencrypted_refused`, `refused_services`, `sessions_timed_out`. | The suites assert the counters beside the refusals; the interop cases assert them after each exchange. |

## Reading checks

- **Error messages** (`source/library/src/kmx/aio/knx/error.cpp`) are fixed strings. No message is formatted with frame
  contents, keys, passwords or addresses.
- **Logging.** The KNX layer logs no frame contents, key material or passwords.
- **Keyring input.** The XML reader refuses DOCTYPE, ENTITY and CDATA, and bounds elements, attributes, depth and value
  size before it allocates. The signature is checked before anything is decrypted. Both have fuzz targets, and each
  ran for one CPU-hour without a finding.
- **Network input.** Every secure codec checks lengths before it reads. The datagram decoder, the TCP reassembler and the
  Data Secure context have fuzz targets (`script/feature/knx/run-fuzz.sh`), seeded from the generated vectors. On
  2026-09-10 each ran for one CPU-hour under AddressSanitizer and UndefinedBehaviorSanitizer without a finding: 1.66
  billion, 934 million and 765 million executions.
- **Concurrency.** Shared secure state is guarded by a lock held for synchronous steps only, never across I/O. The
  concurrency cases run clean under ThreadSanitizer.

## Found while preparing this review

- **All-zero serial on the server.** The server session table sealed its wrappers under whatever serial number it was
  configured with, zero included, where every other secure endpoint refuses it (P8). It now refuses every
  SESSION_REQUEST until it has a valid serial number.
- **Data Secure frame with no APCI.** A transport control frame (data length 0) to a keyed group was secured into a
  telegram no receiver could open. `context::secure_frame` now refuses it as unsupported. The Data Secure fuzz target's
  round trip is what would have caught it.

## Residual risks

- **Device authentication opt-out.** `tunnelling_credentials::skip_device_authentication` lets a client accept any
  server. It is off by default and set only by name.
- **Refusals are visible.** A secure server answers a SESSION_REQUEST past its limits with SESSION_STATUS
  unauthenticated, so a peer can tell that the limits are reached.
- **Data Secure sender tables are not persisted.** After a restart, a receiver's table starts from the sequence numbers
  the keyring recorded, as xknx's does. A telegram captured since that export can be replayed once after each restart.
  Persisting the table is the mitigation; S-A_Sync, which would resynchronise it, is out of scope.
- **Sends racing a closing connection.** A `generic_server::send` that claimed a channel just before its connection
  ended can reach a transport that is being destroyed. This applies to secure and plain TCP channels alike. Applications
  should stop sending on a channel once its connection is gone.

## Sign-off

| Reviewer | Date | Profiles reviewed | Findings raised | Signature |
| :--- | :--- | :--- | :--- | :--- |
| *pending* | | | | |
