# AVB / IEEE 802.1

AVB support includes shared generic components with readiness/completion aliases.

## Main Areas

- raw Ethernet socket usage
- gPTP clock synchronization
- SRP stream reservation

## Runtime Requirements

- `CAP_NET_RAW`
- NIC/driver support for hardware timestamping (PTP)
- VLAN/SRP-capable network path for reservation workflows

Install host-side AVB/PTP runtime tools on Ubuntu/Debian:

```bash
bash script/feature/avb/install-dependencies.sh
```

Or via umbrella bootstrap:

```bash
bash script/bootstrap_optional_deps.sh --avb
```

## Local CI/Smoke Entry

Use the local CI-equivalent script for AVB build-and-test flow:

```bash
bash script/ci/run-ci-avb-local.sh --only all
```

## C++ Key Methods - Readiness model

Readiness aliases use the same AVB flow:

```cpp
// gPTP synchronization gate
readiness::avb::gptp::clock gm(exec);
co_await gm.start(iface);
co_await gm.wait_sync(std::chrono::seconds(5));

// SRP reservation
readiness::avb::srp::client srp(exec);
co_await srp.start(iface);
co_await srp.advertise(desc);     // talker
auto reserved = co_await srp.subscribe(stream_id); // listener

// AVTP socket IO
readiness::avb::eth_socket sock(exec);
co_await sock.open(iface, avb::ethertype::avtp);
co_await sock.send(dest_mac, frame_bytes, presentation_time_ns); // talker
auto recv_result = co_await sock.recv();                         // listener

// AVTP AM824 framing helpers
auto frame = avb::avtp::build_am824_frame(stream_id, seq, presentation_time_ns, payload);
auto view = avb::avtp::parse_am824_frame(std::span<const std::byte>(recv_frame));
```

## C++ Key Methods - Completion model

Completion aliases use the same AVB flow:

```cpp
// gPTP synchronization gate
completion::avb::gptp::clock gm(exec);
co_await gm.start(iface);
co_await gm.wait_sync(std::chrono::seconds(5));

// SRP reservation
completion::avb::srp::client srp(exec);
co_await srp.start(iface);
co_await srp.advertise(desc);     // talker
auto reserved = co_await srp.subscribe(stream_id); // listener

// AVTP socket IO
completion::avb::eth_socket sock(exec);
co_await sock.open(iface, avb::ethertype::avtp);
co_await sock.send(dest_mac, frame_bytes, presentation_time_ns); // talker
auto recv_result = co_await sock.recv();                         // listener

// AVTP AM824 framing helpers
auto frame = avb::avtp::build_am824_frame(stream_id, seq, presentation_time_ns, payload);
auto view = avb::avtp::parse_am824_frame(std::span<const std::byte>(recv_frame));
```

## Full C++ Sample Projects

Complete buildable samples are already available in the repository:

- [source/sample/readiness/avb/talker/sample-avb-readiness-talker.qbs](../../source/sample/readiness/avb/talker/sample-avb-readiness-talker.qbs)
- [source/sample/readiness/avb/talker/src/main.cpp](../../source/sample/readiness/avb/talker/src/main.cpp)
- [source/sample/readiness/avb/talker/src/kmx/aio/sample/avb/talker/manager.cpp](../../source/sample/readiness/avb/talker/src/kmx/aio/sample/avb/talker/manager.cpp)
- [source/sample/readiness/avb/listener/sample-avb-readiness-listener.qbs](../../source/sample/readiness/avb/listener/sample-avb-readiness-listener.qbs)
- [source/sample/readiness/avb/listener/src/main.cpp](../../source/sample/readiness/avb/listener/src/main.cpp)
- [source/sample/readiness/avb/listener/src/kmx/aio/sample/avb/listener/manager.cpp](../../source/sample/readiness/avb/listener/src/kmx/aio/sample/avb/listener/manager.cpp)
- [source/sample/completion/avb/talker/sample-avb-talker.qbs](../../source/sample/completion/avb/talker/sample-avb-talker.qbs)
- [source/sample/completion/avb/listener/sample-avb-listener.qbs](../../source/sample/completion/avb/listener/sample-avb-listener.qbs)

## Quick Validation

Verify CLI parsing and the gPTP timeout path on a single host without a full AVB fabric:

```bash
TALKER_BIN="$(find debug -type f -name sample-avb-talker | head -n 1)"
"$TALKER_BIN" --help
"$TALKER_BIN" --iface eth0 --dest-mac 91:E0:F0:00:0E:80 --stream-id 1 --diagnostics-only
```

For end-to-end AVTP streaming with gPTP synchronization, see [Two-Host Quick Start](#two-host-quick-start) below.

## Two-Host Quick Start

1. Build:

```bash
qbs build -f source/source.qbs config:debug -j"$(nproc)"
```

1. Locate the sample binaries:

```bash
find debug -type f -name sample-avb-talker
find debug -type f -name sample-avb-listener
find debug -type f -name sample-avb-readiness-talker
find debug -type f -name sample-avb-readiness-listener
```

1. Set capabilities:

```bash
sudo setcap cap_net_raw,cap_sys_time+ep /path/to/sample-avb-talker
sudo setcap cap_net_raw,cap_sys_time+ep /path/to/sample-avb-listener
sudo setcap cap_net_raw,cap_sys_time+ep /path/to/sample-avb-readiness-talker
sudo setcap cap_net_raw,cap_sys_time+ep /path/to/sample-avb-readiness-listener
```

1. Start talker on host A:

```bash
/path/to/sample-avb-talker \
    --iface eth0 \
    --dest-mac 91:E0:F0:00:0E:80 \
    --stream-id 1 \
    --sync-timeout-s 5 \
    --period-us 125
```

1. Start listener on host B with the same `--stream-id` and the talker source MAC:

```bash
/path/to/sample-avb-listener \
    --iface eth0 \
    --talker-mac 02:11:22:33:44:55 \
    --stream-id 1 \
    --sync-timeout-s 5 \
    --period-us 125
```

For readiness aliases, use `sample-avb-readiness-talker` and `sample-avb-readiness-listener` with the same argument pattern.

Expected behavior:

- talker/listener report `synced=true`
- listener `parsed` counter advances
- SRP failures appear early as explicit startup errors

Diagnostics-only bring-up:

```bash
/path/to/sample-avb-talker --iface eth0 --dest-mac 91:E0:F0:00:0E:80 --stream-id 1 --diagnostics-only
/path/to/sample-avb-listener --iface eth0 --talker-mac 02:11:22:33:44:55 --stream-id 1 --diagnostics-only
```

## Single-Host Lab Mode

Useful for CLI and failure-path validation only; this does not validate end-to-end AVTP delivery.

Basic CLI validation:

```bash
debug/sample-avb-talker.*/sample-avb-talker --help | head -n 20
debug/sample-avb-listener.*/sample-avb-listener --help | head -n 20
```

MAC parser validation:

```bash
debug/sample-avb-listener.*/sample-avb-listener --iface eth0 --talker-mac not-a-mac --stream-id 1
```

gPTP/SRP timeout-path validation:

```bash
debug/sample-avb-talker.*/sample-avb-talker \
    --iface eth0 \
    --dest-mac 91:E0:F0:00:0E:80 \
    --stream-id 1 \
    --sync-timeout-s 2 \
    --max-frames 1
```

Notes:

- `--talker-mac` is the talker NIC source MAC, not the AVTP multicast destination.
- gPTP synchronization is a startup gate; without a reachable grandmaster, streaming times out before AVTP payload flow.

## Troubleshooting

- `gPTP sync failed`: no grandmaster reachable or PTP path missing.
- `SRP advertise/subscribe failed`: VLAN/SRP path not configured end-to-end.
- listener `parsed=0` with traffic present: `--talker-mac` or `--stream-id` mismatch.
- persistent high jitter: verify `synced`, `offset`, `path_delay`, QoS class, and link health.
