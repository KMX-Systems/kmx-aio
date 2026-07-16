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
bash script/ci/run-ci-avb-local.sh --only all
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

bash script/ci/run-ci-avb-local.sh --only all
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
bash script/ci/run-ci-avb-local.sh --only build-and-test
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
bash script/ci/run-ci-avb-local.sh --only all
```

Run individual CI jobs:

```bash
bash script/ci/run-ci-avb-local.sh --only build-and-test
bash script/ci/run-ci-avb-local.sh --only artifact-split-smoke
bash script/ci/run-ci-avb-local.sh --only quic-smoke
bash script/ci/run-ci-avb-local.sh --only gpu-smoke
```

> **`artifact-split-smoke`** enforces that samples and `library-test` do not depend on `kmx-aio-lib`, that each sample gates optional artifacts with matching `project.enable_*` conditions, then bootstraps local `open62541` and SPDK prefixes and builds the expanded explicit-dependency set.

### QUIC Smoke Test

Requires `project.enable_quic:true`:

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

### Mutual TLS (mTLS) Tests

Two complementary test suites validate mTLS certificate generation, validation, and handling:

#### mTLS Smoke Test

Basic validation of certificate generation and OpenSSL parsing:

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "[tls][mtls][smoke]"
```

Validates:

- Self-signed server certificate and RSA key generation
- Client certificate signed by server key
- PEM format compliance
- File size expectations (RSA 2048 keys >1000 bytes, certificates >300 bytes)
- OpenSSL x509 and RSA key parsing success
- Certificate content non-empty

#### mTLS Integration Tests

Comprehensive testing of certificate scenarios and edge cases:

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "[tls][mtls][integration]"
```

Covers:

- **Certificate chain validation**: Validates file existence, PEM format, OpenSSL parsing
- **Expired certificate handling**: Generates certificates with 1-day expiration and validates handling
- **Certificate identity verification**: Extracts and verifies Common Name (CN) fields
- **Multiple certificate sets**: Tests generation of independent certificate pairs without collision
- **Certificate file operations**: Validates file I/O, size constraints, and format compliance
- **Format validation**: Comprehensive PEM header presence and OpenSSL tool validation

Both test suites automatically generate temporary mTLS artifacts in `/tmp/kmx_mtls_certs/` and clean up after validation.

### OPC UA Service Tests

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "[opc_ua][client][service]~[slow]"
```

### Modbus Tests

Build with Modbus enabled:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.enable_modbus:true

qbs build -f source/source.qbs config:debug -j"$(nproc)" \
    project.enable_modbus:true
```

Run unit tests:

```bash
TEST_BIN="$(find source/debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "[modbus]~[integration]"
```

Run integration tests:

```bash
"$TEST_BIN" "[modbus][integration]"
```

Or use the scripted feature flow:

```bash
bash script/feature/modbus/run-unit-tests.sh
bash script/feature/modbus/run-integration-tests.sh
```

`run-integration-tests.sh` also prepares temporary TLS cert sets under `/tmp/kmx_modbus_certs_exchange` and `/tmp/kmx_modbus_certs_reject` and runs TLS-tagged cases in isolated invocations.

## Scripted Test Orchestration

Repository-level test scripts orchestrate feature scripts based on `KMX_ENABLE_*` flags:

```bash
bash script/run-unit-tests.sh
bash script/run-integration-tests.sh
```

For complete per-feature script behavior and filters, see [Script Reference](scripts.md).

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
