# Build and Feature Gates

## Baseline Build (Dependency-Light)

```bash
qbs resolve -f source/source.qbs config:debug \
    project.enable_openonload:false \
    project.enable_af_xdp:false \
    project.enable_spdk:false \
    project.enable_quic:false

qbs build -f source/source.qbs config:debug \
    project.enable_openonload:false \
    project.enable_af_xdp:false \
    project.enable_spdk:false \
    project.enable_quic:false
```

## Full Build

```bash
qbs resolve -f source/source.qbs config:debug
qbs build -f source/source.qbs config:debug
```

## Common Feature Gates

```bash
# Example: disable SPDK and AF_XDP
qbs build -f source/source.qbs \
    project.enable_spdk:false \
    project.enable_af_xdp:false
```

## SPDK Local Install Build

If you installed SPDK with:

```bash
bash scripts/install_spdk_local.sh
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

If your SPDK build is configured **without crypto** (for example via `scripts/install_spdk_local.sh`),
keep `project.spdk_enable_crypto:false` to avoid linker errors for `-lisal` / `-lisal_crypto`.
If your SPDK installation includes ISA-L crypto support, you can enable it explicitly:

```bash
qbs build -f source/source.qbs config:debug \
    project.enable_spdk:true \
    project.spdk_enable_crypto:true
```

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

Default gate state in [source/source.qbs](source/source.qbs) (current project behavior):

- `project.enable_openonload:true`
- `project.enable_af_xdp:true`
- `project.enable_spdk:true`
- `project.enable_quic:true`
- `project.enable_avb:true`
- `project.enable_cuda:true`
- `project.enable_opc_ua:false`

## Exported Feature Defines

When enabled, `kmx-aio-lib` exports these compile-time defines:

- `KMX_AIO_FEATURE_OPENONLOAD=1`
- `KMX_AIO_FEATURE_AF_XDP=1`
- `KMX_AIO_FEATURE_SPDK=1`
- `KMX_AIO_FEATURE_QUIC=1`
- `KMX_AIO_FEATURE_AVB=1`
- `KMX_AIO_FEATURE_CUDA=1`
- `KMX_AIO_FEATURE_OPC_UA=1` (only if OPC UA is enabled)

If QBS reports profile/config mismatch, run `qbs resolve` first with the same file/profile/config values.

If you need clang-tidy integration, see [Static Analysis](static-analysis.md).
