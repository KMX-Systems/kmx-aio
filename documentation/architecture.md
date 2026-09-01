# Architecture

This repository stays as a single monorepo. The split is at build-artifact level, not at repo level.

## Project Structure

```text
kmx-aio/
├── source/
│   ├── library/          # Core library source code
│   │   ├── api/kmx/aio/  # Public headers
│   │   │   ├── aio.hpp              # Single-include entry point over the whole API
│   │   │   ├── task.hpp, executor_base.hpp, scheduler.hpp, file_descriptor.hpp
│   │   │   ├── async_mutex.hpp      # Mutex acquired with co_await, held across a suspension
│   │   │   ├── basic_channel.hpp, channel.hpp   # SPSC ring: index/backpressure base + typed storage
│   │   │   ├── basic_types.hpp, stream_concepts.hpp, error_code.hpp
│   │   │   ├── ipv4.hpp, ipv6.hpp, mac.hpp      # Address value types
│   │   │   ├── allocator/           # Coroutine-frame slab allocator
│   │   │   │   └── slab.hpp, counter.hpp, statistics.hpp
│   │   │   ├── buffer/              # Fixed-capacity buffer leasing
│   │   │   │   └── pool.hpp, handle.hpp, view/item.hpp
│   │   │   ├── readiness/           # epoll model APIs
│   │   │   │   ├── executor.hpp, io_base.hpp, tcp/, udp/, descriptor/, timer.hpp
│   │   │   │   ├── v4l2/            # V4L2 zero-copy capture
│   │   │   │   ├── tls/, quic/, openonload/, avb/
│   │   │   ├── completion/          # io_uring model APIs
│   │   │   │   ├── executor.hpp, io_base.hpp, tcp/, udp/, timer.hpp
│   │   │   │   ├── v4l2/, xdp/, spdk/, tls/, quic/, avb/
│   │   │   ├── gpu/                 # GPU completion model APIs
│   │   │   │   └── executor.hpp, stream.hpp, event.hpp, basic_types.hpp
│   │   │   ├── tls/                 # Model-agnostic TLS stream
│   │   │   │   └── basic_stream.hpp, stream.hpp
│   │   │   ├── http2/               # HTTP/2 codec, frames, HPACK
│   │   │   ├── http3/               # HTTP/3 codec, QPACK, control/message layer
│   │   │   ├── avb/                 # Audio Video Bridging / IEEE 802.1
│   │   │   │   ├── eth_socket.hpp, avb_types.hpp, avtp/, gptp/, srp/
│   │   │   ├── opc_ua/              # OPC UA facade (feature-gated)
│   │   │   │   └── client.hpp, server.hpp, subscription.hpp, types.hpp, error.hpp
│   │   │   ├── modbus/              # Modbus TCP/TLS facade (feature-gated)
│   │   │   │   └── client.hpp, server.hpp, tls_client.hpp, tls_server.hpp, frame.hpp, types.hpp, error.hpp
│   │   │   ├── someip/              # SOME/IP facade (feature-gated)
│   │   │   │   └── client.hpp, server.hpp, subscription.hpp, types.hpp, error.hpp
│   │   │   └── quic/                # QUIC generic engine
│   │   │       └── engine.hpp, transport.hpp, settings.hpp
│   │   ├── inc/kmx/aio/             # Private headers
│   │   │   ├── detail/syscalls.hpp              # The syscall seam every backend calls through
│   │   │   ├── completion/detail/, tls/detail/, modbus/detail/, allocator/detail/
│   │   │   ├── quic/base_engine.hpp, quic/engine_impl.hpp
│   │   │   ├── avb/base_eth_socket.hpp, avb/gptp/, avb/srp/
│   │   │   └── opc_ua/open62541_compat.hpp, someip/vsomeip_compat.hpp
│   │   ├── src/                     # Implementation (.cpp) files
│   │   ├── prj/                     # Sub-library project files
│   │   │   ├── core.qbs             # kmx-aio-core
│   │   │   ├── readiness.qbs        # kmx-aio-readiness
│   │   │   ├── completion.qbs       # kmx-aio-completion
│   │   │   ├── http2.qbs            # kmx-aio-http2
│   │   │   ├── http3.qbs            # kmx-aio-http3
│   │   │   ├── quic.qbs             # kmx-aio-quic
│   │   │   ├── avb.qbs              # kmx-aio-avb
│   │   │   ├── spdk.qbs             # kmx-aio-spdk
│   │   │   ├── someip.qbs           # kmx-aio-someip
│   │   │   ├── xdp.qbs              # kmx-aio-xdp
│   │   │   ├── opcua.qbs            # kmx-aio-opcua
│   │   │   ├── modbus.qbs           # kmx-aio-modbus
│   │   │   └── gpu.qbs              # kmx-aio-gpu
│   │   ├── library.qbs              # Aggregates split sub-libraries
│   │   └── lib.qbs                  # Umbrella compatibility artifact (kmx-aio-lib)
│   ├── library-test/                # Unit tests and integration tests
│   │   └── unit-test.qbs
│   ├── library-benchmark/           # Benchmark harness and suites (kmx-aio-benchmark)
│   ├── sample/                      # Example applications
│   │   ├── common/                  # Shared sample helpers
│   │   ├── readiness/               # Readiness model samples (epoll)
│   │   │   ├── tcp/                 # TCP echo, minimal server/client
│   │   │   ├── udp/                 # UDP echo, minimal server/client
│   │   │   ├── tls/                 # TLS echo, HTTP/2 ALPN examples
│   │   │   ├── quic/                # QUIC echo server/client on readiness aliases
│   │   │   ├── avb/                 # AVB talker/listener samples on readiness aliases
│   │   │   └── v4l2/                # V4L2 frame capture
│   │   ├── completion/              # Completion model samples (io_uring)
│   │   │   ├── tcp/                 # TCP echo with io_uring
│   │   │   ├── udp/                 # UDP echo with io_uring
│   │   │   ├── tls/                 # TLS echo, HTTP/2 ALPN examples
│   │   │   ├── v4l2/                # V4L2 frame capture (io_uring poll hybrid)
│   │   │   ├── quic/                # QUIC echo server, HTTP/3 server/client
│   │   │   ├── spdk/                # SPDK bdev discovery, minimal block I/O
│   │   │   ├── someip/              # SOME/IP echo, pub/sub, diagnostics samples
│   │   │   ├── xdp/                 # AF_XDP packet filter
│   │   │   ├── avb/                 # AVB talker/listener samples on completion aliases
│   │   │   └── hft/                 # High-frequency trading order router
│   │   └── gpu/                     # GPU samples (own tree; not a model sub-project)
│   │       └── image_processing/    # V4L2 + CUDA async image processing pipeline
│   ├── qbs/modules/                 # Project-local QBS modules (kmx_instrumentation)
│   └── source.qbs                   # Root build definition
└── README.md, LICENSE, etc.
```

Project structure notes:

- The repository stays as one monorepo; library split is at artifact level.
- Feature-specific behavior and commands live under `documentation/features`.
- CI-local and feature bootstrap workflows are centered around scripts in `script/ci` and `script/feature`.
- SOME/IP sample applications are under `source/sample/completion/someip`.
- The GPU sample lives in `source/sample/gpu`, not under a model sub-project: it drives a
  `completion::executor` and a `gpu::executor` side by side and belongs to neither alone.
- The per-artifact QBS files live in `source/library/prj`; `source/library/library.qbs` aggregates them
  and `source/library/lib.qbs` builds the umbrella `kmx-aio-lib`.

## Artifact Graph

Low-level artifact graph:

```text
kmx-aio-core
├── kmx-aio-http2
├── kmx-aio-http3
├── kmx-aio-quic
├── kmx-aio-gpu
├── kmx-aio-modbus
├── kmx-aio-someip
├── kmx-aio-opcua
├── kmx-aio-readiness
│   └── kmx-aio-avb
└── kmx-aio-completion
    ├── kmx-aio-spdk
    └── kmx-aio-xdp
```

Current implementation notes:

- `kmx-aio-avb` depends on `kmx-aio-readiness` because readiness-specific AVB instantiations live there.
- `kmx-aio-quic` depends on `kmx-aio-core` alone and carries one translation unit,
  `src/kmx/aio/quic/transport.cpp`. The per-model explicit instantiations of the engine live with their
  model: `src/kmx/aio/quic/engine.cpp` plus `base_engine.cpp` compile into `kmx-aio-readiness`, and
  `src/kmx/aio/completion/quic/**` plus `base_engine.cpp` into `kmx-aio-completion`, each gated on
  `project.enable_quic`.
- `kmx-aio-http3` is a codec-only artifact over `kmx-aio-core`, built when `project.enable_http3` and
  `project.enable_quic` are both on.
- `kmx-aio-someip` is a standalone feature artifact defined by `source/library/prj/someip.qbs`.
- Enabling QUIC moves the whole project's TLS code from the system OpenSSL to BoringSSL
  (`project.tls_backend`), which is why QUIC cannot share a binary with SPDK or OPC UA. See
  [Build and Feature Gates](build.md#build-with-all-features).
- `kmx-aio-lib` is kept as a compatibility umbrella over all split artifacts.

## Ownership Rules

Public API ownership:

- `source/library/api/kmx/aio/avb/**` belongs to `kmx-aio-avb`.
- `source/library/api/kmx/aio/readiness/**` belongs to `kmx-aio-readiness`.
- `source/library/api/kmx/aio/completion/**` belongs to `kmx-aio-completion`.
- `source/library/api/kmx/aio/http2/**` belongs to `kmx-aio-http2`.
- `source/library/api/kmx/aio/http3/**` belongs to `kmx-aio-http3`.
- `source/library/api/kmx/aio/quic/**` belongs to `kmx-aio-quic`.
- `source/library/api/kmx/aio/gpu/**` belongs to `kmx-aio-gpu`.
- `source/library/api/kmx/aio/opc_ua/**` belongs to `kmx-aio-opcua`.
- `source/library/api/kmx/aio/modbus/**` belongs to `kmx-aio-modbus`.
- `source/library/api/kmx/aio/someip/**` belongs to `kmx-aio-someip`.
- `source/library/api/kmx/aio/completion/spdk/**` belongs to `kmx-aio-spdk`.
- `source/library/api/kmx/aio/completion/xdp/**` belongs to `kmx-aio-xdp`.
- Everything else directly under `source/library/api/kmx/aio/` — the primitives (`task.hpp`,
  `async_mutex.hpp`, `scheduler.hpp`, `channel.hpp`, `allocator/`, `buffer/`), the address types and the
  model-agnostic `tls/` streams — belongs to `kmx-aio-core`.

Model-specific alias headers stay under the model namespace:

- `completion/avb/*` belongs to `kmx-aio-completion`.
- `readiness/avb/*` belongs to `kmx-aio-readiness`.

Private implementation ownership:

- `source/library/inc/kmx/aio/quic/**` is private to `kmx-aio-quic`.
- `source/library/inc/kmx/aio/avb/**` is private to `kmx-aio-avb`.
- `source/library/inc/kmx/aio/opc_ua/**` is private to `kmx-aio-opcua`.
- `source/library/inc/kmx/aio/modbus/**` is private to `kmx-aio-modbus`.
- `source/library/inc/kmx/aio/someip/**` is private to `kmx-aio-someip`.
- `source/library/inc/kmx/aio/detail/**`, `allocator/detail/**` and `tls/detail/**` are private to
  `kmx-aio-core`; `completion/detail/**` is private to `kmx-aio-completion`.

The `detail` headers are private on purpose. `detail/syscalls.hpp` is the seam every backend makes its
system calls through, and `project.enable_fault_injection` compiles a faulting policy into it so a test
can make one call fail — a consumer including it directly would be depending on a build-mode-dependent
implementation detail.

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
