# Testing Workflow

## CI-Equivalent Local Script

```bash
bash scripts/ci/run-ci-avb-local.sh --only all
```

Run a single CI-equivalent job:

```bash
bash scripts/ci/run-ci-avb-local.sh --only build-and-test
bash scripts/ci/run-ci-avb-local.sh --only artifact-split-smoke
bash scripts/ci/run-ci-avb-local.sh --only quic-smoke
bash scripts/ci/run-ci-avb-local.sh --only gpu-smoke
```

> **`artifact-split-smoke`** enforces that sample and `library-test` consumer QBS products no longer depend on `kmx-aio-lib`, that each sample gates optional artifacts with the matching `project.enable_*` condition, then bootstraps local `open62541` and SPDK prefixes and builds the expanded explicit-dependency set.

## Regular Local Tests

```bash
qbs build -f source/source.qbs config:debug -j"$(nproc)"
bash build/run-tests-bounded.sh
```

If you want readiness or HTTP/3 tests included, enable those project sets explicitly during build first:

```bash
qbs build -f source/source.qbs config:debug -j"$(nproc)" \
    project.enable_readiness:true \
    project.enable_http3:true
```

Run a focused flake guard repeatedly:

```bash
RUNS=40 TIMEOUT_SECONDS=20 TEST_FILTER="channel wait_until_can_send unblocks when consumer pops from a full ring" \
    bash build/run-tests-bounded.sh
```

## Sanitizers

```bash
bash build/run-sanitizer-tests.sh
```

Use one sanitizer at a time:

- `project.enable_asan:true`
- `project.enable_tsan:true`

## Targeted Smoke Tests

### QUIC smoke

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "[quic][readiness][integration][smoke]"
"$TEST_BIN" "[quic][http3][integration][smoke]"
```

The readiness smoke requires `project.enable_readiness:true` at build time.
The HTTP/3 smoke requires `project.enable_http3:true` in addition to `project.enable_quic:true`.

Optional QUIC smoke tuning variables:

- `KMX_AIO_QUIC_READINESS_WATCHDOG_NS` (default `10000000`)
- `KMX_QUIC_ECHO_PORT` (default `12345`)
- `KMX_QUIC_HTTP3_PORT` (default `12345`)

### OPC UA service tests

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "[opc_ua][client][service]~[slow]"
```

### GPU smoke

```bash
qbs build --products sample-gpu-image-processing,kmx-aio-test -f source/source.qbs config:debug -j"$(nproc)" \
    project.enable_openonload:false \
    project.enable_af_xdp:false \
    project.enable_spdk:false \
    project.enable_quic:false \
    project.enable_cuda:true

SAMPLE_BIN="$(find debug -type f -name sample-gpu-image-processing | head -n 1)"
LD_LIBRARY_PATH=/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-} \
    "$SAMPLE_BIN" --max-frames 1 --width 320 --height 240 --buffer-count 2 --gpu-device 0
```
