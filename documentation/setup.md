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

Recommended:

```bash
bash scripts/install_optional_accelerator_deps.sh
```

Equivalent manual install:

```bash
sudo apt update
sudo apt install -y \
    libbpf-dev libxdp-dev libelf-dev zlib1g-dev clang llvm \
    libaio-dev libnuma-dev uuid-dev meson ninja-build libssl-dev
```

## Optional Features Bootstrap (Scripts)

Run all optional dependency installers/checks from repository root:

```bash
bash scripts/bootstrap_optional_deps.sh --all
```

The script is additive and idempotent-friendly: rerunning it only refreshes state or verifies prerequisites.
If a step needs elevated privileges, you will be prompted by the underlying installer script.

Use targeted flags when you only need specific features:

```bash
bash scripts/bootstrap_optional_deps.sh --af-xdp --quic --opc-ua
```

Supported flags:

- `--all`: runs all optional installers/checks in sequence
- `--accelerators`: installs shared optional accelerator/system dependencies
- `--af-xdp`: installs AF_XDP prerequisites (`libbpf`, `libxdp`, tooling)
- `--avb`: installs AVB/PTP host runtime tools
- `--spdk`: bootstraps local SPDK under `build/spdk-local`
- `--v4l2`: installs optional V4L2 host tooling
- `--quic`: builds BoringSSL and lsquic dependencies
- `--opc-ua`: builds open62541 local prefix
- `--cuda-check`: validates CUDA environment (driver/toolkit presence)

Common bootstrap flows:

```bash
# Network acceleration and packet I/O
bash scripts/bootstrap_optional_deps.sh --af-xdp --avb

# QUIC + OPC UA stack
bash scripts/bootstrap_optional_deps.sh --quic --opc-ua

# Storage and media path
bash scripts/bootstrap_optional_deps.sh --spdk --v4l2
```

## Optional Feature Prerequisites

- QUIC / HTTP/3: BoringSSL + lsquic (run [scripts/install_lsquic.sh](scripts/install_lsquic.sh))
- OPC UA: open62541 (run [scripts/install_open62541.sh](scripts/install_open62541.sh))
- AF_XDP: libbpf/libxdp toolchain available (run [scripts/install_af_xdp_deps.sh](scripts/install_af_xdp_deps.sh))
- SPDK: local workspace bootstrap available (run [scripts/install_spdk_local.sh](scripts/install_spdk_local.sh))
- AVB: host runtime tools available (run [scripts/install_avb_deps.sh](scripts/install_avb_deps.sh)); still requires `CAP_NET_RAW` and PTP-capable NIC/driver
- CUDA: validate environment with [scripts/check_cuda_env.sh](scripts/check_cuda_env.sh) (driver/toolkit install is distro-specific)
- V4L2: optional host tooling available (run [scripts/install_v4l2_deps.sh](scripts/install_v4l2_deps.sh))

## Verify Key Libraries

```bash
pkg-config --modversion liburing
pkg-config --modversion libbpf
pkg-config --modversion libxdp
pkg-config --modversion spdk_nvme
```

## Build BoringSSL and lsquic (TLS/QUIC)

```bash
bash scripts/install_lsquic.sh
```

## Build open62541 (OPC UA)

```bash
bash scripts/install_open62541.sh
```

Then build with:

```bash
qbs build -f source/source.qbs config:debug -j"$(nproc)" \
    project.enable_opc_ua:true \
    project.opc_ua_vendored:true \
    project.opc_ua_prefix:"$PWD/build/open62541/install-local"
```

For in-depth SPDK and environment troubleshooting, see [SPDK feature docs](features/spdk.md).
