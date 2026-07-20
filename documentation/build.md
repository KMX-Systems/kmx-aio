# Build and Feature Gates

## Default Build

Current default active graph:

- `kmx-aio-core`
- `kmx-aio-completion`
- `kmx-aio-quic`

Everything else is disabled by default unless explicitly enabled.

```bash
qbs resolve -f source/source.qbs config:debug
qbs build -f source/source.qbs config:debug
```

This builds the test binary `kmx-aio-test` with only core, completion, and QUIC tests. Tests for readiness, AVB, OPC-UA, GPU, and other optional features are **not** included.

## Build All Tests

To build the complete test suite including tests for all optional features:

```bash
qbs resolve -f source/source.qbs config:debug project.full:true
qbs build -f source/source.qbs config:debug --products kmx-aio-test project.full:true
```

Or selectively enable only the features you need:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.enable_readiness:true \
    project.enable_avb:true

qbs build -f source/source.qbs config:debug --products kmx-aio-test \
    project.enable_readiness:true \
    project.enable_avb:true
```

## Baseline Build

```bash
qbs resolve -f source/source.qbs config:debug

qbs build -f source/source.qbs config:debug
```

## Enable Additional Project Sets

Readiness model:

```bash
qbs build -f source/source.qbs config:debug \
    project.enable_readiness:true
```

HTTP/2 stack:

```bash
qbs build -f source/source.qbs config:debug \
    project.enable_http2:true
```

HTTP/3 demo stack on top of QUIC:

```bash
qbs build -f source/source.qbs config:debug \
    project.enable_http3:true
```

Readiness + HTTP/3 together:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.enable_readiness:true \
    project.enable_http3:true

qbs build -f source/source.qbs config:debug \
    project.enable_readiness:true \
    project.enable_http3:true
```

## Build With All Features

To enable every optional feature gate in this repository, use one aggregate switch:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.full:true

qbs build -f source/source.qbs config:debug \
    project.full:true
```

`project.all:true` is accepted as an alias and behaves the same.

Note: aggregate flags are strict. With `project.full:true` (or `project.all:true`),
all optional gates are activated. During resolve/build, QBS runs
`script/bootstrap_optional_deps.sh` with the required feature flags so missing
third-party dependencies are downloaded/built/installed automatically.

### CI / Non-Interactive Environments

Automatic bootstrap may need package-manager access (`apt`, `dnf`, etc.) and
root privileges. In CI, use one of these patterns:

- Pre-provision a build image with required system packages.
- Provide passwordless sudo for the CI user.

Recommended preflight in CI:

```bash
sudo -n true
```

If this fails, dependency bootstrap cannot install missing system packages
non-interactively, and the resolve/build step will fail.

If you prefer an explicit template that you can tweak per gate, use:

```bash
qbs build -f source/source.qbs config:debug \
    project.enable_readiness:true \
    project.enable_http2:true \
    project.enable_quic:true \
    project.enable_http3:true \
    project.enable_modbus:true \
    project.enable_openonload:true \
    project.enable_af_xdp:true \
    project.enable_spdk:true \
    project.enable_avb:true \
    project.enable_opc_ua:true \
    project.enable_someip:true \
    project.enable_cuda:true
```

If you use a non-default SPDK or OPC UA install prefix, pass those as well:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.full:true \
    project.spdk_prefix:"$PWD/build/spdk-local/install-local" \
    project.opc_ua_vendored:true \
    project.opc_ua_prefix:"$PWD/build/open62541/install-local"

qbs build -f source/source.qbs config:debug \
    project.full:true \
    project.spdk_prefix:"$PWD/build/spdk-local/install-local" \
    project.opc_ua_vendored:true \
    project.opc_ua_prefix:"$PWD/build/open62541/install-local"
```

You can still disable any specific gate explicitly even with `project.full:true`, for example:

```bash
qbs build -f source/source.qbs config:debug \
    project.full:true \
    project.enable_spdk:false
```

## Common Feature Gates

```bash
# Example: disable SPDK and AF_XDP
qbs build -f source/source.qbs \
    project.enable_spdk:false \
    project.enable_af_xdp:false
```

## OPC-UA Local Install Build

If you installed OPC-UA with:

```bash
bash script/feature/opc_ua/install-dependencies.sh
```

then open62541 is installed under `build/open62541/install-local`.
The project default `project.opc_ua_prefix` now points to this local path.
Pass the prefix explicitly during resolve/build so headers like `open62541.h` are found:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.enable_opc_ua:true \
    project.opc_ua_prefix:"$PWD/build/open62541/install-local"

qbs build -f source/source.qbs config:debug \
    project.enable_opc_ua:true \
    project.opc_ua_prefix:"$PWD/build/open62541/install-local"
```

## SPDK Local Install Build

If you installed SPDK with:

```bash
bash script/feature/spdk/install-dependencies.sh
```

then SPDK is installed under `build/spdk-local/install-local`, not `/usr/local`.
The project default `project.spdk_prefix` now points to this local path.
Pass the prefix explicitly during resolve/build so headers like `spdk/bdev.h` are found:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.enable_spdk:true \
    project.spdk_prefix:"$PWD/build/spdk-local/install-local"

qbs build -f source/source.qbs config:debug \
    project.enable_spdk:true \
    project.spdk_prefix:"$PWD/build/spdk-local/install-local"
```

If you do not need SPDK for a build, disable it:

```bash
qbs build -f source/source.qbs config:debug project.enable_spdk:false
```

If your SPDK installation is in `/usr/local` (or another prefix), override `project.spdk_prefix`:

```bash
qbs build -f source/source.qbs config:debug \
    project.enable_spdk:true \
    project.spdk_prefix:"/usr/local"
```

SPDK links ISA-L (`-lisal`, `-lisal_crypto`) when the SPDK feature is enabled,
which matches current runtime and link requirements on supported environments.

## Persistent QBS Profile For Local SPDK

To avoid repeating `project.spdk_prefix` on every command, create a dedicated profile once:

```bash
qbs config --add-profile kmx-spdk-local \
    project.enable_spdk true \
    project.spdk_prefix "$PWD/build/spdk-local/install-local"
```

Then use that profile for resolve/build:

```bash
qbs resolve -f source/source.qbs config:debug profile:kmx-spdk-local
qbs build -f source/source.qbs config:debug profile:kmx-spdk-local
```

To inspect or remove the profile:

```bash
qbs config --list profiles.kmx-spdk-local
qbs config --unset profiles.kmx-spdk-local
```

Default gate state in [source/source.qbs](../source/source.qbs) (current project behavior):

- `project.full:false`
- `project.all:false`
- `project.enable_readiness:false`
- `project.enable_completion:true`
- `project.enable_http2:false`
- `project.enable_http3:false`
- `project.enable_openonload:false`
- `project.enable_af_xdp:false`
- `project.enable_spdk:false`
- `project.enable_quic:true`
- `project.enable_avb:false`
- `project.enable_opc_ua:false`
- `project.enable_modbus:false`
- `project.enable_someip:false`
- `project.enable_cuda:false`

## Exported Feature Defines

When enabled, `kmx-aio-lib` exports these compile-time defines:

- `KMX_AIO_FEATURE_OPENONLOAD=1`
- `KMX_AIO_FEATURE_AF_XDP=1`
- `KMX_AIO_FEATURE_SPDK=1`
- `KMX_AIO_FEATURE_QUIC=1`
- `KMX_AIO_FEATURE_AVB=1`
- `KMX_AIO_FEATURE_OPC_UA=1` (only if OPC UA is enabled)
- `KMX_AIO_FEATURE_MODBUS=1`
- `KMX_AIO_FEATURE_SOMEIP=1`
- `KMX_AIO_FEATURE_CUDA=1`

If QBS reports profile/config mismatch, run `qbs resolve` first with the same file/profile/config values.

If you need clang-tidy integration, see [Static Analysis](static-analysis.md).
