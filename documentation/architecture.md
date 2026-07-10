# Architecture

This repository stays as a single monorepo. The split is at build-artifact level, not at repo level.

## Project Structure

```text
kmx-aio/
├── source/
│   ├── library/          # Core library source code
│   │   ├── api/kmx/aio/  # Public headers
│   │   │   ├── task.hpp, executor_base.hpp, file_descriptor.hpp, allocator.hpp, error_code.hpp
│   │   │   ├── readiness/           # epoll model APIs
│   │   │   │   ├── executor.hpp, tcp/, udp/, descriptor/, timer.hpp
│   │   │   │   ├── v4l2/            # V4L2 zero-copy capture
│   │   │   │   ├── tls/, quic/, openonload/, avb/
│   │   │   ├── completion/          # io_uring model APIs
│   │   │   │   ├── executor.hpp, tcp/, udp/, timer.hpp
│   │   │   │   ├── v4l2/, xdp/, spdk/, tls/, quic/, avb/
│   │   │   ├── gpu/                 # GPU completion model APIs
│   │   │   │   └── executor.hpp     # executor, stream, and event public API
│   │   │   ├── http2/               # HTTP/2 codec, frames, HPACK
│   │   │   ├── avb/                 # Audio Video Bridging / IEEE 802.1
│   │   │   │   ├── eth_socket.hpp, gptp/, srp/
│   │   │   ├── opc_ua/              # OPC UA facade (feature-gated)
│   │   │   │   └── client.hpp, server.hpp, subscription.hpp, types.hpp, error.hpp
│   │   │   ├── someip/               # SOME/IP facade (feature-gated)
│   │   │   │   └── client.hpp, server.hpp, subscription.hpp, types.hpp, error.hpp
│   │   │   └── quic/                # QUIC generic engine
│   │   ├── inc/kmx/aio/             # Private headers (opc_ua/open62541_compat.hpp, quic/base_engine.hpp, ...)
│   │   ├── src/                     # Implementation (.cpp) files
│   │   ├── core/core.qbs            # kmx-aio-core
│   │   ├── readiness/readiness.qbs  # kmx-aio-readiness
│   │   ├── completion/completion.qbs# kmx-aio-completion
│   │   ├── http2/http2.qbs          # kmx-aio-http2
│   │   ├── quic/quic.qbs            # kmx-aio-quic
│   │   ├── avb/avb.qbs              # kmx-aio-avb
│   │   ├── spdk/spdk.qbs            # kmx-aio-spdk
│   │   ├── someip/someip.qbs        # kmx-aio-someip
│   │   ├── xdp/xdp.qbs              # kmx-aio-xdp
│   │   ├── opcua/opcua.qbs          # kmx-aio-opcua
│   │   ├── gpu/gpu.qbs              # kmx-aio-gpu
│   │   ├── library.qbs              # Aggregates split sub-libraries
│   │   └── lib.qbs                  # Umbrella compatibility artifact (kmx-aio-lib)
│   ├── library-test/                # Unit tests and integration tests
│   │   └── unit-test.qbs
│   ├── sample/                      # Example applications
│   │   ├── readiness/               # Readiness model samples (epoll)
│   │   │   ├── tcp/                 # TCP echo, minimal server/client
│   │   │   ├── udp/                 # UDP echo, minimal server/client
│   │   │   ├── tls/                 # TLS echo, HTTP/2 ALPN examples
│   │   │   ├── avb/                 # AVB talker/listener samples on readiness aliases
│   │   │   └── v4l2/                # V4L2 frame capture
│   │   └── completion/              # Completion model samples (io_uring)
│   │       ├── tcp/                 # TCP echo with io_uring
│   │       ├── udp/                 # UDP echo with io_uring
│   │       ├── tls/                 # TLS echo, HTTP/2 ALPN examples
│   │       ├── v4l2/                # V4L2 frame capture (io_uring poll hybrid)
│   │       ├── quic/                # QUIC echo server, HTTP/3 server/client
│   │       ├── spdk/                # SPDK bdev discovery, minimal block I/O
│   │       ├── someip/              # SOME/IP echo, pub/sub, diagnostics samples
│   │       ├── xdp/                 # AF_XDP packet filter
│   │       ├── hft/                 # High-frequency trading order router
│   │       └── gpu/                 # GPU completion model samples
│   │          └── image_processing/ # V4L2 + CUDA async image processing pipeline
│   └── source.qbs                   # Root build definition
└── README.md, LICENSE, etc.
```

Project structure notes:

- The repository stays as one monorepo; library split is at artifact level.
- Feature-specific behavior and commands live under `documentation/features`.
- CI-local and feature bootstrap workflows are centered around scripts in `script/ci` and `script/feature`.
- SomeIP sample applications are under `source/sample/completion/someip`.

## Artifact Graph

Low-level artifact graph:

```text
kmx-aio-core
├── kmx-aio-http2
├── kmx-aio-readiness
│   └── kmx-aio-quic
├── kmx-aio-completion
│   ├── kmx-aio-spdk
│   └── kmx-aio-xdp
├── kmx-aio-avb
├── kmx-aio-someip
├── kmx-aio-gpu
├── kmx-aio-opcua
```

Current implementation notes:

- `kmx-aio-avb` depends on `kmx-aio-readiness` because readiness-specific AVB instantiations live there.
- `kmx-aio-quic` depends on `kmx-aio-readiness`; completion-specific QUIC explicit instantiation now lives in `kmx-aio-completion`.
- `kmx-aio-someip` is a standalone feature artifact under `source/library/someip`.
- `kmx-aio-lib` is kept as a compatibility umbrella over all split artifacts.

## Ownership Rules

Public API ownership:

- `source/library/api/kmx/aio/avb/**` belongs to `kmx-aio-avb`.
- `source/library/api/kmx/aio/readiness/**` belongs to `kmx-aio-readiness`.
- `source/library/api/kmx/aio/completion/**` belongs to `kmx-aio-completion`.
- `source/library/api/kmx/aio/http2/**` belongs to `kmx-aio-http2`.
- `source/library/api/kmx/aio/quic/**` belongs to `kmx-aio-quic`.
- `source/library/api/kmx/aio/gpu/**` belongs to `kmx-aio-gpu`.
- `source/library/api/kmx/aio/opc_ua/**` belongs to `kmx-aio-opcua`.
- `source/library/api/kmx/aio/someip/**` belongs to `kmx-aio-someip`.
- `source/library/api/kmx/aio/completion/spdk/**` belongs to `kmx-aio-spdk`.
- `source/library/api/kmx/aio/completion/xdp/**` belongs to `kmx-aio-xdp`.

Model-specific alias headers stay under the model namespace:

- `completion/avb/*` belongs to `kmx-aio-completion`.
- `readiness/avb/*` belongs to `kmx-aio-readiness`.

Private implementation ownership:

- `source/library/inc/kmx/aio/quic/**` is private to `kmx-aio-quic`.
- `source/library/inc/kmx/aio/avb/**` is private to `kmx-aio-avb`.
- `source/library/inc/kmx/aio/opc_ua/**` is private to `kmx-aio-opcua`.

## Validation Strategy

Representative standalone consumers already exist and should keep building:

- readiness TCP sample
- completion QUIC sample
- completion SPDK discovery/minimal samples
- completion AVB talker/listener samples
- readiness AVB talker/listener samples

CI coverage:

- `build-and-test`: dependency-light umbrella validation
- `artifact-split-smoke`: sample and test-consumer boundary guard plus expanded explicit sub-library consumer validation with local `open62541` and `SPDK` prefixes
- `quic-smoke`: QUIC/HTTP3 integration smoke
- `gpu-smoke`: CUDA sample + GPU-tagged tests when hardware is available
