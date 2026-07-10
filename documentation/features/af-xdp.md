# AF_XDP

AF_XDP support is completion-model only and feature-gated.

## Components

- `completion::xdp::socket`
- UMEM management and RX/TX/fill/completion rings
- eBPF-friendly packet filtering integration points

## Build Gate

- `project.enable_af_xdp:true` (default)

## Dependencies

- `libbpf-dev`
- `libxdp-dev`
- `libelf-dev`
- `zlib1g-dev`
- `clang`, `llvm`

Install them on Ubuntu/Debian with:

```bash
bash script/feature/af_xdp/install-dependencies.sh
```

Or via umbrella bootstrap:

```bash
bash script/bootstrap_optional_deps.sh --af-xdp
```

## C++ Key Methods - Completion model

Minimal API surface used by the AF_XDP sample:

```cpp
using namespace kmx::aio;
using namespace completion::xdp;

const socket_config cfg {
    .interface_name = iface,
    .queue_id = queue_id,
    .frame_size = 4096u,
    .frame_count = 4096u,
    .fill_ring_size = 2048u,
    .comp_ring_size = 2048u,
    .rx_ring_size = 2048u,
    .tx_ring_size = 2048u,
    .force_zero_copy = false,
    .need_wakeup = true,
};

auto sock_result = socket::create(exec, cfg);
if (!sock_result)
    co_return;

auto sock = std::move(*sock_result);

for (;;)
{
    auto recv_result = co_await sock.recv();
    if (!recv_result)
    {
        if (recv_result.error() == std::make_error_code(std::errc::operation_would_block))
            sock.trigger_wakeup();
        break;
    }

    auto& frame = *recv_result;
    auto payload = std::span<const std::byte>(frame.data.data(), frame.length);

    co_await sock.send(payload);
    sock.release_frame(frame.addr);
}

const auto& stats = sock.get_stats();
```

## Full C++ Sample Project

The full implementation is already available in the QBS sample:

- [source/sample/completion/xdp/packet_filter/xdp-packet-filter.qbs](source/sample/completion/xdp/packet_filter/xdp-packet-filter.qbs)
- [source/sample/completion/xdp/packet_filter/src/main.cpp](source/sample/completion/xdp/packet_filter/src/main.cpp)
- [source/sample/completion/xdp/packet_filter/src/kmx/aio/sample/xdp/packet_filter/manager.cpp](source/sample/completion/xdp/packet_filter/src/kmx/aio/sample/xdp/packet_filter/manager.cpp)

## Build and Run

Build and run the AF_XDP packet-filter sample:

```bash
qbs build -f source/source.qbs config:debug project.enable_af_xdp:true
XDP_BIN="$(find debug -type f -name sample-xdp-packet-filter | head -n 1)"
"$XDP_BIN" --help
```
