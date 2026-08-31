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
- `--spdk`: bootstraps local SPDK under `output/spdk-local`
- `--v4l2`: installs optional V4L2 host tooling
- `--quic`: uses an installed BoringSSL/lsquic when it is new enough, otherwise builds the pinned versions
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

- QUIC / HTTP/3: BoringSSL + lsquic (run [script/feature/quic/install-dependencies.sh](../script/feature/quic/install-dependencies.sh))
- Modbus: no extra system packages currently required (run [script/feature/modbus/install-dependencies.sh](../script/feature/modbus/install-dependencies.sh) for consistency with feature workflows)
- OPC UA: open62541 (run [script/feature/opc_ua/install-dependencies.sh](../script/feature/opc_ua/install-dependencies.sh))
- AF_XDP: libbpf/libxdp toolchain available (run [script/feature/af_xdp/install-dependencies.sh](../script/feature/af_xdp/install-dependencies.sh))
- SPDK: local workspace bootstrap available (run [script/feature/spdk/install-dependencies.sh](../script/feature/spdk/install-dependencies.sh))
- AVB: host runtime tools available (run [script/feature/avb/install-dependencies.sh](../script/feature/avb/install-dependencies.sh)); still requires `CAP_NET_RAW` and PTP-capable NIC/driver
- CUDA: validate environment with [script/feature/cuda/check_env.sh](../script/feature/cuda/check_env.sh) (driver/toolkit install is distro-specific)
- V4L2: optional host tooling available (run [script/feature/v4l2/install-dependencies.sh](../script/feature/v4l2/install-dependencies.sh))

## Verify Key Libraries

```bash
pkg-config --modversion liburing
pkg-config --modversion libbpf
pkg-config --modversion libxdp
pkg-config --modversion spdk_nvme
```

## BoringSSL and lsquic (TLS/QUIC)

```bash
bash script/feature/quic/install-dependencies.sh
```

The script looks for each library on the system before downloading anything, and only clones and builds
the pinned version when the library is missing or older than what this project is known to build against:

| Library | Accepted when | Checked through |
| --- | --- | --- |
| BoringSSL | `BORINGSSL_API_VERSION` >= 36 | `<prefix>/include/openssl/base.h` plus `libssl`/`libcrypto` under the same prefix |
| lsquic | `LSQUIC_MAJOR_VERSION` >= 4 | `<prefix>/include/lsquic.h` plus `liblsquic` under the same prefix |

Searched prefixes are `$BORINGSSL_PREFIX` / `$LSQUIC_PREFIX` (when set), then `/usr/local`, then `/usr`.
OpenSSL is never accepted in place of BoringSSL: the QUIC transport's ALPN callback and lsquic's crypto
backend both use BoringSSL-only entry points, so the header must declare `OPENSSL_IS_BORINGSSL`. An
installed lsquic is only used when an installed BoringSSL was accepted as well, because the two are
compiled against each other.

Useful overrides:

- `KMX_QUIC_FORCE_VENDORED=1` - ignore whatever is installed and build the pinned versions
- `BORINGSSL_MIN_API_VERSION`, `LSQUIC_MIN_MAJOR_VERSION` - move the acceptance thresholds
- `BORINGSSL_REF`, `LSQUIC_REF` - pick different revisions to build

The outcome is written to `output/quic-dependencies.json`, and `source/source.qbs` reads that file to
decide what to compile and link against, so the build always follows the same decision the script made.

### One TLS implementation per binary

OpenSSL and BoringSSL export the same symbol names for types that are laid out differently, so mixing
the headers of one with the libraries of the other produces no diagnostic - just an `SSL_CTX` whose
fields are read at the wrong offsets. Every product therefore takes its TLS provider from a single
project property:

| Build | `project.tls_backend` | TLS code compiles and links against |
| --- | --- | --- |
| `project.enable_quic:true` (implied by `enable_http3`) | `boringssl` | the BoringSSL resolved above |
| QUIC disabled | `openssl` | the system OpenSSL |

QUIC decides because lsquic's backend and the ALPN selection callback are BoringSSL-only. Set
`project.tls_backend` explicitly to override. Nothing in the tree names `ssl`/`crypto` directly any
more; products use `project.tls_libraries` and `project.tls_include_paths`.

The exception is a build that enables QUIC together with SPDK or OPC UA: those two are prebuilt against
the system OpenSSL and carry it into the link as a transitive dependency. Qbs warns when that
combination is configured - rebuild them against BoringSSL, or keep the features in separate binaries.

## Build open62541 (OPC UA)

```bash
bash script/feature/opc_ua/install-dependencies.sh
```

Then build with:

```bash
qbs build -f source/source.qbs config:debug -j"$(nproc)" \
    project.enable_opc_ua:true \
    project.opc_ua_vendored:true \
    project.opc_ua_prefix:"$PWD/output/open62541/install-local"
```

For in-depth SPDK and environment troubleshooting, see [SPDK feature docs](features/spdk.md).

For full script behavior across all features and top-level orchestrators, see [Script Reference](scripts.md).
