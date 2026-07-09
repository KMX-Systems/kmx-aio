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
