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
bash script/feature/cuda/install-dependencies.sh
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
bash script/bootstrap_optional_deps.sh --all
```

The script is additive and idempotent-friendly: rerunning it only refreshes state or verifies prerequisites.
If a step needs elevated privileges, you will be prompted by the underlying installer script.

Use targeted flags when you only need specific features:

```bash
bash script/bootstrap_optional_deps.sh --af-xdp --quic --opc-ua
```

For V4L2, either run the dedicated installer or enable the feature and use the top-level installer:

```bash
bash script/feature/v4l2/install-dependencies.sh
KMX_ENABLE_V4L2=true bash script/install-dependencies.sh
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
bash script/bootstrap_optional_deps.sh --af-xdp --avb

# QUIC + OPC UA stack
bash script/bootstrap_optional_deps.sh --quic --opc-ua

# Storage and media path
bash script/bootstrap_optional_deps.sh --spdk --v4l2
```

## Optional Feature Prerequisites

Note on defaults:

- The default project graph enables `core + completion + quic`.
- Readiness, HTTP/2, HTTP/3, AVB, AF_XDP, SPDK, OPC UA, CUDA, and OpenOnload remain off until explicitly enabled with `project.enable_*:true`.

- QUIC / HTTP/3: BoringSSL + lsquic (run [script/feature/quic/install-dependencies.sh](script/feature/quic/install-dependencies.sh))
- Modbus: no extra system packages currently required (run [script/feature/modbus/install-dependencies.sh](script/feature/modbus/install-dependencies.sh) for consistency with feature workflows)
- OPC UA: open62541 (run [script/feature/opc_ua/install-dependencies.sh](script/feature/opc_ua/install-dependencies.sh))
- AF_XDP: libbpf/libxdp toolchain available (run [script/feature/af_xdp/install-dependencies.sh](script/feature/af_xdp/install-dependencies.sh))
- SPDK: local workspace bootstrap available (run [script/feature/spdk/install-dependencies.sh](script/feature/spdk/install-dependencies.sh))
- AVB: host runtime tools available (run [script/feature/avb/install-dependencies.sh](script/feature/avb/install-dependencies.sh)); still requires `CAP_NET_RAW` and PTP-capable NIC/driver
- CUDA: validate environment with [script/feature/cuda/check_env.sh](script/feature/cuda/check_env.sh) (driver/toolkit install is distro-specific)
- V4L2: optional host tooling available (run [script/feature/v4l2/install-dependencies.sh](script/feature/v4l2/install-dependencies.sh))

## Verify Key Libraries

```bash
pkg-config --modversion liburing
pkg-config --modversion libbpf
pkg-config --modversion libxdp
pkg-config --modversion spdk_nvme
```

## Build BoringSSL and lsquic (TLS/QUIC)

```bash
bash script/feature/quic/install-dependencies.sh
```

## Build open62541 (OPC UA)

```bash
bash script/feature/opc_ua/install-dependencies.sh
```

Then build with:

```bash
qbs build -f source/source.qbs config:debug -j"$(nproc)" \
    project.enable_opc_ua:true \
    project.opc_ua_vendored:true \
    project.opc_ua_prefix:"$PWD/build/open62541/install-local"
```

For in-depth SPDK and environment troubleshooting, see [SPDK feature docs](features/spdk.md).

For full script behavior across all features and top-level orchestrators, see [Script Reference](scripts.md).
