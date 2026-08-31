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

`script/run-sanitizer-tests.sh` builds the project instrumented and runs the unit tests against it:

```bash
bash script/run-sanitizer-tests.sh              # AddressSanitizer + UndefinedBehaviorSanitizer
bash script/run-sanitizer-tests.sh asan
bash script/run-sanitizer-tests.sh ubsan
bash script/run-sanitizer-tests.sh tsan
bash script/run-sanitizer-tests.sh tsan+ubsan
```

Feature selection works exactly as it does for `script/run-unit-tests.sh`:

```bash
KMX_ENABLE_READINESS=true KMX_ENABLE_QUIC=true bash script/run-sanitizer-tests.sh asan+ubsan
```

Each selection builds into its own tree — `output/asan-ubsan`, `output/tsan`, and so on — so an
instrumented build never displaces the ordinary `output/debug` one, and no uninstrumented
`kmx-aio-test` is left where the instrumented one is expected.

ASan and TSan cannot be combined: they ship mutually exclusive runtimes, and the build rejects the
combination rather than producing a binary that half works. UBSan combines with either.

### Building Under A Sanitizer By Hand

The runner is a wrapper over project properties, so a build from the command line or from Qt Creator
sets the same switches:

- `project.enable_asan:true` — AddressSanitizer
- `project.enable_ubsan:true` — UndefinedBehaviorSanitizer
- `project.enable_tsan:true` — ThreadSanitizer

```bash
cd source
qbs build -f source.qbs -d ../output/asan config:debug -j"$(nproc)" \
    project.enable_asan:true project.enable_ubsan:true
cd ..

TEST_BIN="$(find output/asan -type f -name kmx-aio-test | head -n 1)"
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    LD_LIBRARY_PATH="/opt/gcc-16/lib64:${LD_LIBRARY_PATH:-}" timeout 120s "$TEST_BIN"
```

The flags themselves live in one place, the `kmx_instrumentation` QBS module under
`source/qbs/modules/`, which every product depends on and every library re-exports. That is what puts
the same `-fsanitize=` on the libraries, the samples and the test binary: a static library compiled
with ASan needs the executable linking it to bring in the ASan runtime, or the link fails outright.

### Runtime Options

`script/feature/common.sh` sets the environment every instrumented binary it launches needs, and only
fills in a variable the caller has not already set:

| Variable | Default the runner applies |
| :--- | :--- |
| `ASAN_OPTIONS` | `detect_leaks=1:detect_stack_use_after_return=1:strict_string_checks=1:check_initialization_order=1:print_stacktrace=1` |
| `UBSAN_OPTIONS` | `print_stacktrace=1:halt_on_error=1` |
| `TSAN_OPTIONS` | `halt_on_error=1:second_deadlock_stack=1` |
| `LSAN_OPTIONS` | `suppressions=script/sanitizer/lsan.supp` |

`halt_on_error=1` is what makes a UBSan finding fail the run: UBSan otherwise recovers from every
check and the process still exits 0.

Suppressions live in `script/sanitizer/lsan.supp` and `script/sanitizer/tsan.supp`, and are picked up
automatically. Both start empty of active rules — a leak or race in this library's own code is a bug
to fix, not one to suppress; the files are for allocations and synchronisation that genuinely belong
to a third-party library.

## Coverage

`script/run-coverage.sh` builds with `gcov` instrumentation, runs the tests, and turns the counters
into a report:

```bash
bash script/run-coverage.sh                  # unit tests, lcov report + HTML
bash script/run-coverage.sh --integration    # unit and integration tests
bash script/run-coverage.sh --no-html        # tracefile and summary only
bash script/run-coverage.sh --gcov-only      # plain gcov listings, no lcov
```

The instrumented build goes to `output/coverage`, and the report to `output/coverage-report`:

| Path | Contents |
| :--- | :--- |
| `output/coverage-report/coverage.info` | lcov tracefile, filtered down to `source/library` |
| `output/coverage-report/html/index.html` | browsable report, from `genhtml` |
| `output/coverage-report/gcov/` | per-file `.gcov` listings, from `--gcov-only` or when lcov is missing |

lcov is optional. Without it the script falls back to plain `gcov` and prints a per-file summary; the
report is then per translation unit, so a header included by several `.cpp` files is listed once for
each of them. Merging those is what lcov adds:

```bash
sudo apt-get install lcov        # Debian/Ubuntu
sudo dnf install lcov            # Fedora/RHEL
```

The `.gcno` and `.gcda` files carry a format version stamp that has to match the compiler exactly, and
these builds use a GCC that is usually newer than the system one. The script therefore uses the `gcov`
sitting next to the profile's compiler rather than whatever is first in `PATH`; `GCOV=/path/to/gcov`
overrides that choice.

Counters accumulate across runs by design, so each run clears them first. Pass `--keep-data` to
combine several runs into one report on purpose.

Coverage can be combined with a sanitizer — `project.enable_coverage:true` alongside
`project.enable_asan:true` — though the two are usually more informative apart.

### Building With Coverage By Hand

```bash
cd source
qbs build -f source.qbs -d ../output/coverage config:debug -j"$(nproc)" project.enable_coverage:true
```

This compiles with `--coverage -fprofile-update=atomic -fprofile-abs-path`. The atomic counter updates
matter here: this library is threaded throughout, and the default non-atomic updates lose increments
when two threads take the same arc, which reads as covered lines being reported cold.
