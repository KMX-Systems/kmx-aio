# TLS and HTTP/2

## TLS

TLS streams are exposed through `kmx::aio::tls::stream<InnerStream>` and available in both execution models.

- BoringSSL-backed
- Memory BIO state machine design
- Supports ALPN for protocol negotiation (for example HTTP/2)
- The state machine itself lives in the non-template base `kmx::aio::tls::basic_stream`: SSL and BIO
  ownership, the handshake, the record read and write loops and the ALPN configuration name no
  transport, so they are compiled once rather than per instantiation. `tls::stream<InnerStream>` adds
  the transport it holds and the two forwarding calls that move bytes to and from it.

### Threading

One reader and one writer may run concurrently on the same stream, from different threads — `read()` in
one coroutine while `write()` or `write_all()` runs in another. That is what a full-duplex protocol
needs, and what a raw `SSL` cannot give: OpenSSL forbids two threads touching one `SSL` at a time, and
its memory BIOs are no safer.

Three locks inside `basic_stream` are what make it hold:

| Lock | Covers | Held across a suspension |
| :--- | :--- | :--- |
| `engine_mutex_` | every `SSL_*` and `BIO_*` call | no — crypto only, never I/O |
| `read_pump_mutex_` | a transport read *and* the BIO write that files it | yes |
| `write_pump_mutex_` | taking a record out of the write BIO *and* putting it on the wire | yes |

The two pump locks are the ones that matter for correctness rather than for crashes. Without the read
pump lock, two coroutines each take a slice of the byte stream and hand them to OpenSSL in whatever
order they finished, which decrypts as garbage. Without the write pump lock, records interleave on the
wire and the peer's session breaks instead of this one.

Not supported:

- **Two concurrent readers, or two concurrent writers.** They will not corrupt the session, but they
  will split the byte stream between them. The supported shape is one of each.
- **`handshake()` alongside `read()` or `write()`.** It drives both directions and has to finish first.
- **`native_handle()` while another coroutine is reading or writing.** The raw `SSL` is outside all of
  the above.

Serialization costs two uncontended mutex acquisitions per record on the half-duplex path.

## HTTP/2

HTTP/2 components are model-agnostic and include:

- codec
- stream
- frame
- HPACK support

This stack can be used in both readiness and completion-based applications.

## C++ Key Methods - Readiness model

Minimal API flow used by TLS + HTTP/2 ALPN samples:

```cpp
// Use TLS_server_method() on the server side, TLS_client_method() on the client side.
SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());

// Wrap TCP stream in TLS stream
auto tcp = kmx::aio::readiness::tcp::stream(exec, std::move(fd));
kmx::aio::readiness::tls::stream stream(std::move(tcp), ctx);

// ALPN — server registers a callback on the CTX; client calls set_alpn_protocols on the stream
SSL_CTX_set_alpn_select_cb(ctx, alpn_select_cb, nullptr);               // server side
stream.set_alpn_protocols(std::array<std::uint8_t, 3>{2, 'h', '2'});   // client side

stream.set_accept_state();   // server
stream.set_connect_state();  // client
co_await stream.handshake();

if (stream.selected_alpn() == "h2")
{
	// HTTP/2 frame exchange over TLS transport
	co_await stream.write_all(std::span<const char>(frame_bytes, frame_size));
	auto read_result = co_await stream.read(std::span<char>(recv_buf, recv_len));
}
```

## C++ Key Methods - Completion model

Identical flow using the completion (io_uring) TCP stream — only the two namespace prefixes differ:

```cpp
// Use TLS_server_method() on the server side, TLS_client_method() on the client side.
SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());

// Wrap TCP stream in TLS stream
auto tcp = kmx::aio::completion::tcp::stream(exec, std::move(fd));
kmx::aio::completion::tls::stream stream(std::move(tcp), ctx);

// ALPN — server registers a callback on the CTX; client calls set_alpn_protocols on the stream
SSL_CTX_set_alpn_select_cb(ctx, alpn_select_cb, nullptr);               // server side
stream.set_alpn_protocols(std::array<std::uint8_t, 3>{2, 'h', '2'});   // client side

stream.set_accept_state();   // server
stream.set_connect_state();  // client
co_await stream.handshake();

if (stream.selected_alpn() == "h2")
{
	// HTTP/2 frame exchange over TLS transport
	co_await stream.write_all(std::span<const char>(frame_bytes, frame_size));
	auto read_result = co_await stream.read(std::span<char>(recv_buf, recv_len));
}
```

## Full C++ Sample Projects

Complete buildable samples are already available in the repository:

**TLS echo (plain, no HTTP/2):**

- [source/sample/completion/tls/echo_completion_server/tls-echo-completion-server.qbs](../../source/sample/completion/tls/echo_completion_server/tls-echo-completion-server.qbs)
- [source/sample/completion/tls/echo_completion_client/tls-echo-completion-client.qbs](../../source/sample/completion/tls/echo_completion_client/tls-echo-completion-client.qbs)
- [source/sample/readiness/tls/echo_readiness_server/tls-echo-readiness-server.qbs](../../source/sample/readiness/tls/echo_readiness_server/tls-echo-readiness-server.qbs)
- [source/sample/readiness/tls/echo_readiness_client/tls-echo-readiness-client.qbs](../../source/sample/readiness/tls/echo_readiness_client/tls-echo-readiness-client.qbs)

**TLS + HTTP/2 ALPN negotiation:**

- [source/sample/completion/tls/h2_alpn_server/tls-h2-alpn-server.qbs](../../source/sample/completion/tls/h2_alpn_server/tls-h2-alpn-server.qbs)
- [source/sample/completion/tls/h2_alpn_server/src/main.cpp](../../source/sample/completion/tls/h2_alpn_server/src/main.cpp)
- [source/sample/completion/tls/h2_alpn_server/src/kmx/aio/sample/tls/h2_alpn_server/manager.cpp](../../source/sample/completion/tls/h2_alpn_server/src/kmx/aio/sample/tls/h2_alpn_server/manager.cpp)
- [source/sample/completion/tls/h2_alpn_client/tls-h2-alpn-client.qbs](../../source/sample/completion/tls/h2_alpn_client/tls-h2-alpn-client.qbs)
- [source/sample/completion/tls/h2_alpn_client/src/main.cpp](../../source/sample/completion/tls/h2_alpn_client/src/main.cpp)
- [source/sample/completion/tls/h2_alpn_client/src/kmx/aio/sample/tls/h2_alpn_client/manager.cpp](../../source/sample/completion/tls/h2_alpn_client/src/kmx/aio/sample/tls/h2_alpn_client/manager.cpp)
- [source/sample/readiness/tls/h2_alpn_server/tls-h2-alpn-readiness-server.qbs](../../source/sample/readiness/tls/h2_alpn_server/tls-h2-alpn-readiness-server.qbs)
- [source/sample/readiness/tls/h2_alpn_server/src/main.cpp](../../source/sample/readiness/tls/h2_alpn_server/src/main.cpp)
- [source/sample/readiness/tls/h2_alpn_server/src/kmx/aio/sample/tls/h2_alpn_server/manager.cpp](../../source/sample/readiness/tls/h2_alpn_server/src/kmx/aio/sample/tls/h2_alpn_server/manager.cpp)
- [source/sample/readiness/tls/h2_alpn_client/tls-h2-alpn-readiness-client.qbs](../../source/sample/readiness/tls/h2_alpn_client/tls-h2-alpn-readiness-client.qbs)
- [source/sample/readiness/tls/h2_alpn_client/src/main.cpp](../../source/sample/readiness/tls/h2_alpn_client/src/main.cpp)
- [source/sample/readiness/tls/h2_alpn_client/src/kmx/aio/sample/tls/h2_alpn_client/manager.cpp](../../source/sample/readiness/tls/h2_alpn_client/src/kmx/aio/sample/tls/h2_alpn_client/manager.cpp)

## Example

Run the TLS/ALPN sample pair:

```bash
SERVER_BIN="$(find debug -type f -name sample-tls-h2-alpn-server | head -n 1)"
CLIENT_BIN="$(find debug -type f -name sample-tls-h2-alpn-client | head -n 1)"
"$SERVER_BIN" &
"$CLIENT_BIN"
```
