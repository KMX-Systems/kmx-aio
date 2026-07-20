# HTTP/2

HTTP/2 ([RFC 7540](https://www.rfc-editor.org/rfc/rfc7540)) support is feature-gated (`project.enable_http2:true`) and has **no executor affinity** — the codec, frame builder, HPACK encoder, and stream state machine are pure data-processing components that sit on top of any TLS stream regardless of whether it uses readiness or completion I/O.

## Scope

- **`http2::frame`** — 9-byte frame header type and a `make_goaway` builder
- **`http2::frame_builder`** — static methods for SETTINGS, SETTINGS ACK, HEADERS, DATA frames
- **`http2::hpack_encoder`** — minimal zero-dependency HPACK literal encoder (RFC 7541)
- **`http2::stream`** — per-stream state machine following RFC 7540 §5.1
- No HTTP/2 server or client class — callers compose the primitives with a TLS stream

## Frame Types

All ten [RFC 7540](https://www.rfc-editor.org/rfc/rfc7540) frame types are enumerated:

```cpp
enum class frame_type : std::uint8_t {
    data, headers, priority, rst_stream, settings,
    push_promise, ping, goaway, window_update, continuation
};
```

The packed 9-byte `frame_header` struct matches the wire layout:

```
+-----------------------------------------------+
|                Length (24 bits)               |
+---------------+---------------+---------------+
|   Type (8)    |   Flags (8)   |
+-+-------------+---------------+-------------------------------+
|R|                Stream Identifier (31 bits)                 |
+=+=============================================================+
```

## Frame Builder

`frame_builder` writes complete binary frames into a caller-supplied `std::span<std::uint8_t>`.

```cpp
#include <kmx/aio/http2/codec.hpp>

std::array<std::uint8_t, 256> buf;

// Write SETTINGS frame (empty, client preface)
auto n = kmx::aio::http2::frame_builder::make_settings(buf);

// Write SETTINGS ACK
n = kmx::aio::http2::frame_builder::make_settings_ack(buf);

// Write HEADERS frame with HPACK-encoded headers
kmx::aio::http2::header_list headers = {
    {":method",    "GET"},
    {":path",      "/"},
    {":scheme",    "https"},
    {":authority", "example.com"},
};
n = kmx::aio::http2::frame_builder::make_headers(buf, 1u, false, headers);

// Write DATA frame (end_stream = true)
n = kmx::aio::http2::frame_builder::make_data(buf, 1u, true, "hello");
```

## HPACK Encoder

The encoder implements "Literal Header Field without Indexing" (RFC 7541 §6.2.2), which requires no dynamic table and no Huffman tables:

```cpp
#include <kmx/aio/http2/hpack.hpp>

using namespace kmx::aio::http2;

header_list headers = {{":status", "200"}, {"content-type", "text/plain"}};

// Pre-compute required buffer size
const std::size_t needed = hpack_encoder::encoded_size(headers);

std::vector<std::uint8_t> buf(needed);
hpack_encoder::encode(buf, headers);     // fills the buffer

// Single header
std::array<std::uint8_t, 64> single_buf;
hpack_encoder::encode_literal(single_buf, "x-custom", "value");
```

## Stream State Machine

`http2::stream` tracks per-stream state transitions as defined in [RFC 7540 §5.1](https://www.rfc-editor.org/rfc/rfc7540#section-5.1):

```
idle → open → half_closed_local → closed
           └→ half_closed_remote ┘
```

```cpp
#include <kmx/aio/http2/stream.hpp>

kmx::aio::http2::stream s {1u};          // stream ID 1

s.on_frame_sent(frame_type::headers, false);   // idle → open
s.on_frame_sent(frame_type::data, true);       // open → half_closed_local
s.on_frame_received(frame_type::headers, true);// half_closed_local → closed
```

`on_frame_sent` and `on_frame_received` throw `std::logic_error` on invalid transitions.

## GOAWAY Frame

```cpp
#include <kmx/aio/http2/frame.hpp>

std::array<std::uint8_t, 17> buf;
// Graceful shutdown: last processed stream = 3, error = NO_ERROR (0)
auto n = kmx::aio::http2::make_goaway(buf, 3u, 0u);
```

## Typical Integration with TLS Stream

```cpp
// Send HTTP/2 client connection preface (RFC 7540 §3.5)
constexpr std::string_view preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
co_await tls_stream.write_all(std::span{preface.data(), preface.size()});

// Send SETTINGS
std::array<std::uint8_t, 64> frame_buf;
auto n = frame_builder::make_settings(frame_buf);
co_await tls_stream.write_all(std::span{frame_buf.data(), n});

// Receive peer SETTINGS and ACK it
// ... read frame header, parse type, send SETTINGS ACK ...
n = frame_builder::make_settings_ack(frame_buf);
co_await tls_stream.write_all(std::span{frame_buf.data(), n});
```

See the TLS/ALPN samples (`sample-tls-h2-alpn-*`) for full HTTP/2 + ALPN negotiation examples.

## Build

```bash
qbs build -f source/source.qbs config:debug \
    project.enable_http2:true \
    -j"$(nproc)"
```

## Source References

| File | Purpose |
| :--- | :--- |
| [source/library/api/kmx/aio/http2/frame.hpp](../../source/library/api/kmx/aio/http2/frame.hpp) | `frame_type`, `frame_header`, `make_goaway` |
| [source/library/api/kmx/aio/http2/hpack.hpp](../../source/library/api/kmx/aio/http2/hpack.hpp) | `hpack_encoder`, `header_list`, `header_field` |
| [source/library/api/kmx/aio/http2/stream.hpp](../../source/library/api/kmx/aio/http2/stream.hpp) | `stream`, `stream_state` |
| [source/library/api/kmx/aio/http2/codec.hpp](../../source/library/api/kmx/aio/http2/codec.hpp) | `frame_builder` (SETTINGS, HEADERS, DATA) |
