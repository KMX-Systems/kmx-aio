# Setup and Dependencies

## Runtime / System Dependencies

- Linux kernel interfaces: `epoll`, sockets, `timerfd`, and `io_uring`
- POSIX networking headers (`arpa/inet.h`, `netinet/in.h`, socket APIs)
- `liburing-dev` for completion model headers/runtime

## Test-Only Dependencies

- [Catch2](https://github.com/catchorg/Catch2) for `kmx-aio-test`

## Mandatory Dependencies (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y liburing-dev build-essential pkg-config git python3
```

## Optional Accelerator Dependencies (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y \
    libbpf-dev libxdp-dev libelf-dev zlib1g-dev clang llvm \
    libaio-dev libnuma-dev uuid-dev meson ninja-build libssl-dev
```

## Optional Feature Prerequisites

- QUIC / HTTP/3: BoringSSL + lsquic (run [build/install_lsquic.sh](build/install_lsquic.sh))
- OPC UA: open62541 (run [build/install_open62541.sh](build/install_open62541.sh))
- AF_XDP: libbpf/libxdp toolchain available
- SPDK: SPDK runtime and pkg-config metadata available
- AVB: `CAP_NET_RAW` and PTP-capable NIC/driver
- CUDA: NVIDIA driver + CUDA toolkit available

## Verify Key Libraries

```bash
pkg-config --modversion liburing
pkg-config --modversion libbpf
pkg-config --modversion libxdp
pkg-config --modversion spdk_nvme
```

## Build BoringSSL and lsquic (TLS/QUIC)

```bash
bash build/install_lsquic.sh
```

## Build open62541 (OPC UA)

```bash
bash build/install_open62541.sh
```

Then build with:

```bash
qbs build -f source/source.qbs config:debug -j"$(nproc)" \
    project.enable_opc_ua:true \
    project.opc_ua_vendored:true \
    project.opc_ua_prefix:"$PWD/build/open62541/install-local"
```

For in-depth SPDK and environment troubleshooting, see [SPDK feature docs](features/spdk.md).
