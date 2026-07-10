# Testing Workflow

## Quick Start

### Run All Unit Tests

```bash
cd source
qbs build -f source.qbs config:debug -j"$(nproc)"
cd ..

TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
if [[ -d /opt/gcc-16/lib64 ]]; then
    LD_LIBRARY_PATH="/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}" "$TEST_BIN"
else
    "$TEST_BIN"
fi
```

### Run All Integration Tests (CI-Equivalent)

```bash
bash scripts/ci/run-ci-avb-local.sh --only all
```

### Run All Tests (Unit + Integration)

```bash
cd source
qbs build -f source.qbs config:debug -j"$(nproc)" \
    project.enable_readiness:true \
    project.enable_http3:true
cd ..

TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
if [[ -d /opt/gcc-16/lib64 ]]; then
    LD_LIBRARY_PATH="/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}" "$TEST_BIN"
else
    "$TEST_BIN"
fi

bash scripts/ci/run-ci-avb-local.sh --only all
```

## Unit Tests

Build and run the main test suite:

```bash
cd source
qbs build -f source.qbs config:debug -j"$(nproc)"
cd ..

TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
if [[ -d /opt/gcc-16/lib64 ]]; then
    LD_LIBRARY_PATH="/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}" "$TEST_BIN"
else
    "$TEST_BIN"
fi
```

Or use the CI-equivalent:

```bash
bash scripts/ci/run-ci-avb-local.sh --only build-and-test
```

To include readiness and HTTP/3 tests, build with those flags first:

```bash
cd source
qbs build -f source.qbs config:debug -j"$(nproc)" \
    project.enable_readiness:true \
    project.enable_http3:true
cd ..

TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
if [[ -d /opt/gcc-16/lib64 ]]; then
    LD_LIBRARY_PATH="/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}" "$TEST_BIN"
else
    "$TEST_BIN"
fi
```

Run a specific test repeatedly (flake guard):

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
for i in $(seq 1 40); do
    echo "Run $i"
    if [[ -d /opt/gcc-16/lib64 ]]; then
        LD_LIBRARY_PATH="/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}" timeout 20s "$TEST_BIN" "channel wait_until_can_send unblocks when consumer pops from a full ring"
    else
        timeout 20s "$TEST_BIN" "channel wait_until_can_send unblocks when consumer pops from a full ring"
    fi
done
```

## Integration Tests

### Full CI-Equivalent Suite

Run all CI jobs locally:

```bash
bash scripts/ci/run-ci-avb-local.sh --only all
```

Run individual CI jobs:

```bash
bash scripts/ci/run-ci-avb-local.sh --only build-and-test
bash scripts/ci/run-ci-avb-local.sh --only artifact-split-smoke
bash scripts/ci/run-ci-avb-local.sh --only quic-smoke
bash scripts/ci/run-ci-avb-local.sh --only gpu-smoke
```

> **`artifact-split-smoke`** enforces that samples and `library-test` do not depend on `kmx-aio-lib`, that each sample gates optional artifacts with matching `project.enable_*` conditions, then bootstraps local `open62541` and SPDK prefixes and builds the expanded explicit-dependency set.

### QUIC Smoke Test

Requires `project.enable_quic:true` (enabled by default):

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "[quic][readiness][integration][smoke]"
"$TEST_BIN" "[quic][http3][integration][smoke]"
```

The readiness smoke requires `project.enable_readiness:true` at build time.
The HTTP/3 smoke requires `project.enable_http3:true` at build time.

Optional environment variables:

- `KMX_AIO_QUIC_READINESS_WATCHDOG_NS` (default `10000000`)
- `KMX_QUIC_ECHO_PORT` (default `12345`)
- `KMX_QUIC_HTTP3_PORT` (default `12345`)

### OPC UA Service Tests

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "[opc_ua][client][service]~[slow]"
```

### GPU Smoke Test

Build with GPU support disabled network optimizations:

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

## Sanitizers

Run tests with a specific sanitizer by setting the QBS project property during build:

- `project.enable_asan:true` — AddressSanitizer
- `project.enable_tsan:true` — ThreadSanitizer

Example:

```bash
cd source
qbs build -f source.qbs config:debug -j"$(nproc)" project.enable_asan:true
cd ..

TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
if [[ -d /opt/gcc-16/lib64 ]]; then
    LD_LIBRARY_PATH="/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}" timeout 120s "$TEST_BIN"
else
    timeout 120s "$TEST_BIN"
fi
```
