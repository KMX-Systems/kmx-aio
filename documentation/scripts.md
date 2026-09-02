# Script Reference

This document is the authoritative reference for repository automation scripts used to install dependencies and run tests.

## Global Orchestration Scripts

These are the top-level scripts requested for full documentation.

| Script | Purpose | Behavior |
| :--- | :--- | :--- |
| `script/install-dependencies.sh` | Global dependency orchestrator | Sources `script/feature/common.sh`, iterates `feature_list`, and runs each feature `install-dependencies.sh` only when the feature is enabled via `KMX_ENABLE_<FEATURE>` or default state. |
| `script/run-unit-tests.sh` | Global unit-test orchestrator | Resolves/builds `source/source.qbs` in `config:debug` using feature args from `build_qbs_feature_args`, then runs enabled feature `run-unit-tests.sh` scripts. |
| `script/run-integration-tests.sh` | Global integration-test orchestrator | Runs enabled feature `run-integration-tests.sh` scripts against pre-built binaries. It auto-detects active features from `kmx-aio-test --list-tags` when `KMX_ENABLE_*` vars are not explicitly set. |
| `script/run-sanitizer-tests.sh` | Sanitizer runner | Takes `asan`, `ubsan`, `asan+ubsan` (the default), `tsan` or `tsan+ubsan`. Builds into a tree of its own (`output/asan-ubsan`, ...) with the matching `project.enable_*` properties, sets `ASAN_OPTIONS`/`UBSAN_OPTIONS`/`TSAN_OPTIONS`/`LSAN_OPTIONS`, then delegates to `script/run-unit-tests.sh`. |
| `script/run-benchmarks.sh` | Benchmark runner | Builds `kmx-aio-benchmark` in `config:release` with readiness and completion enabled, pins the process to one CPU per physical core, reports the CPU governor, and passes its arguments (`--filter`, `--scale`, `--repeats`) through to the binary. See [Benchmarking](benchmarking.md). |
| `script/run-coverage.sh` | Coverage runner | Builds with `project.enable_coverage:true` into `output/coverage`, runs the unit tests (`--integration` adds the integration suites), and writes an lcov tracefile, HTML report and per-file summary to `output/coverage-report`. Falls back to plain `gcov` listings when lcov is absent, or on `--gcov-only`. |
| `script/gcc_full_build.sh` | Whole-tree build with GCC | Names the GCC profile candidates (`gcc16`, `gcc-16`, `gcc13`, `gcc`) and the `output/full-gcc/` build root, then forwards everything to `script/full-build.sh`. |
| `script/clang_full_build.sh` | Whole-tree build with clang | Names the clang profile candidates (`clang-20`, `clang20`, `clang`) and the `output/full-clang/` build root. Adds `-no-pie` when the installed Catch2 archives are not position-independent, then forwards to `script/full-build.sh`. |
| `script/full-build.sh` | Whole-tree build driver | Compiles every translation unit in the tree as several internally consistent feature sets, one build root per set. Not an entry point on its own — the two wrappers above select the toolchain. See [Whole-Tree Builds](#whole-tree-builds). |

## Feature Enablement Model

`script/feature/common.sh` defines execution behavior used by global scripts:

- Feature list order:
  - `completion`, `readiness`, `http2`, `http3`, `openonload`, `af_xdp`, `spdk`, `quic`, `modbus`, `avb`, `opc_ua`, `someip`, `v4l2`, `cuda`
- Default enabled feature:
  - `completion` only
- Environment variable override format:
  - `KMX_ENABLE_<UPPERCASE_FEATURE>` with truthy values `1|true|yes|on` and falsy values `0|false|no|off`
- Integration script auto-detection:
  - `script/run-integration-tests.sh` inspects tags in `source/debug/**/kmx-aio-test` and sets implicit feature enablement from compiled test coverage (for example `af_xdp -> [xdp]`, `cuda -> [gpu]`).
  - Explicitly provided `KMX_ENABLE_*` values always override auto-detected values.
- Dependency propagation:
  - Enabling `modbus` or `v4l2` forces `project.enable_readiness:true` in generated QBS args.
- Build tree location:
  - `KMX_BUILD_ROOT` overrides the `output/` build root that `qbs_build_dir_args` points at, and restricts the `kmx-aio-test` search to that tree so an instrumented run cannot pick up a plain binary from `output/debug`. The sanitizer and coverage runners set it.
- Instrumentation propagation:
  - `KMX_SANITIZERS` (`asan`, `ubsan`, `tsan`, or a `+` combination) and `KMX_COVERAGE` become `qbs_instrumentation_args`, which every script that drives qbs passes along. Without that, a feature script rebuilding the tree for its own feature would resolve the project uninstrumented and replace the binary under test.
- Fault injection:
  - `project.enable_fault_injection` compiles the syscall seam's faulting policy in, so a test can make a system call fail on demand. It follows `KMX_COVERAGE` by default, because the error branches it reaches are otherwise unreachable from a test and would show up as uncovered. `KMX_FAULT_INJECTION` overrides that in either direction, for measuring a build without the seam.

## Per-Feature Script Matrix

All current feature script directories and behavior:

| Feature | Install Script | Unit Test Script | Integration Test Script | Notes |
| :--- | :--- | :--- | :--- | :--- |
| `af_xdp` | `script/feature/af_xdp/install-dependencies.sh` | `script/feature/af_xdp/run-unit-tests.sh` (`[xdp]~[integration]`) | `script/feature/af_xdp/run-integration-tests.sh` (`[xdp][integration]`) | Installs AF_XDP toolchain packages on Ubuntu/Debian and verifies tooling. |
| `avb` | `script/feature/avb/install-dependencies.sh` | `script/feature/avb/run-unit-tests.sh` (`[avb]~[integration]`) | `script/feature/avb/run-integration-tests.sh` (`[avb][integration]`) | Installs AVB/PTP runtime dependencies and tools. |
| `completion` | `script/feature/completion/install-dependencies.sh` | `script/feature/completion/run-unit-tests.sh` (`[completion]~[integration]`) | `script/feature/completion/run-integration-tests.sh` (`[completion][integration]`) | No additional dependency install step required. |
| `cuda` | `script/feature/cuda/install-dependencies.sh` | `script/feature/cuda/run-unit-tests.sh` (`[gpu]~[integration]`) | `script/feature/cuda/run-integration-tests.sh` (`[gpu][integration]`) | Includes environment validation via `script/feature/cuda/check_env.sh` (`nvidia-smi`, headers, optional `nvcc`). |
| `http2` | `script/feature/http2/install-dependencies.sh` | `script/feature/http2/run-unit-tests.sh` (`[http2]~[integration]`) | `script/feature/http2/run-integration-tests.sh` (`[http2][integration]`) | No additional dependency install step required. |
| `http3` | `script/feature/http3/install-dependencies.sh` | `script/feature/http3/run-unit-tests.sh` (`[http3]~[integration]`) | `script/feature/http3/run-integration-tests.sh` (`[http3][integration]`) | Installer delegates to QUIC dependency bootstrap (BoringSSL/lsquic path). |
| `modbus` | `script/feature/modbus/install-dependencies.sh` | `script/feature/modbus/run-unit-tests.sh` (`[modbus]~[integration]`) | `script/feature/modbus/run-integration-tests.sh` | Integration script creates/reuses TLS certs under `/tmp/kmx_modbus_certs_*`, runs `[modbus][integration]~[tls]`, then executes TLS tests in isolated invocations. |
| `opc_ua` | `script/feature/opc_ua/install-dependencies.sh` | `script/feature/opc_ua/run-unit-tests.sh` (`[opc_ua]~[integration]`) | `script/feature/opc_ua/run-integration-tests.sh` (`[opc_ua][integration]`) | Bootstraps local `open62541` into `output/open62541/install-local`. |
| `openonload` | `script/feature/openonload/install-dependencies.sh` | `script/feature/openonload/run-unit-tests.sh` (`[openonload]~[integration]`) | `script/feature/openonload/run-integration-tests.sh` (`[openonload][integration]`) | Verifies OpenOnload prerequisites and host support state. |
| `quic` | `script/feature/quic/install-dependencies.sh` | `script/feature/quic/run-unit-tests.sh` | `script/feature/quic/run-integration-tests.sh` (`[quic][readiness][integration][smoke][slow]` and `[quic][http3][readiness][integration][smoke][slow]`) | Unit script currently prints no dedicated QUIC unit tests. Install script uses an installed BoringSSL/lsquic when new enough, otherwise builds the pinned versions under `output/`. |
| `readiness` | `script/feature/readiness/install-dependencies.sh` | `script/feature/readiness/run-unit-tests.sh` (`[readiness]~[integration]`) | `script/feature/readiness/run-integration-tests.sh` (`[readiness][integration]`) | No additional dependency install step required. |
| `someip` | `script/feature/someip/install-dependencies.sh` | `script/feature/someip/run-unit-tests.sh` (`[someip]~[integration]`) | `script/feature/someip/run-integration-tests.sh` (`[someip][integration]`) | Unit/integration scripts perform SOME/IP-enabled `qbs resolve`/`qbs build` before tests. Installer can prompt to install missing distro packages and builds local vsomeip prefix. |
| `spdk` | `script/feature/spdk/install-dependencies.sh` | `script/feature/spdk/run-unit-tests.sh` (`[spdk]~[integration]`) | `script/feature/spdk/run-integration-tests.sh` (`[spdk][integration]`) | Installs build deps and bootstraps local SPDK under `output/spdk-local/install-local`, then runs `script/feature/spdk/install-isal.sh` to reconcile ISA-L into that prefix. |
| `v4l2` | `script/feature/v4l2/install-dependencies.sh` | `script/feature/v4l2/run-unit-tests.sh` (`[v4l2][model]`) | `script/feature/v4l2/run-integration-tests.sh` | Integration script currently reports no dedicated V4L2 integration tests. |

## Additional Feature-Specific Utilities

| Script | Purpose |
| :--- | :--- |
| `script/feature/cuda/check_env.sh` | Non-invasive environment check for CUDA runtime/toolkit readiness. |
| `script/feature/pic.sh` | `library_needs_pic_rebuild` — reports whether an already-built dependency archive still carries absolute relocations (`R_X86_64_32`), which a PIE link rejects. Every feature installer sources this so a tree built before `-DCMAKE_POSITION_INDEPENDENT_CODE=ON` is rebuilt rather than kept. The OPC UA installer adds a second check of its own for GCC LTO archives, which only the GCC driver can link. |
| `script/feature/someip/run-smoke.sh` | End-to-end SOME/IP smoke runner with optional `--skip-build` and `--skip-samples`. Builds SOME/IP targets, runs `[someip]`, then executes sample binaries/log checks. |

## Whole-Tree Builds

No single set of feature flags compiles the whole tree. `project.full:true` turns every gate on at once,
which puts QUIC next to SPDK and OPC UA in one binary: QUIC moves the TLS code to BoringSSL, while SPDK
and open62541 are prebuilt against the system OpenSSL. The two export the same symbol names for types
laid out differently, so the link succeeds without a diagnostic and the binary reads an `SSL_CTX` at the
wrong offsets at run time. `source/source.qbs` warns about exactly that combination.

Compiling every translation unit is therefore several builds. `script/full-build.sh` runs them, each set
internally consistent and the sets together leaving no `.cpp` uncompiled:

| Set | Features enabled on top of core + completion | Why it is separate |
| :--- | :--- | :--- |
| `quic` | `readiness openonload http2 http3 quic modbus cuda` | Carries BoringSSL, so nothing linked against the system OpenSSL may join it. |
| `storage` | `readiness openonload af_xdp spdk opc_ua someip modbus` | The OpenSSL half: SPDK and open62541 are prebuilt against it, and vsomeip sits next to them. |
| `avb` | `readiness openonload af_xdp avb modbus v4l2` | AVB pulls in the gPTP/SRP tree, which nothing else compiles. |

`readiness` and `openonload` appear in every set: neither conflicts with anything, and both change what
the other features compile.

Each set builds into `output/full-<toolchain>/<set>`, so the passes cannot overwrite one another's
artifacts and rebuilding one set does not throw the others away.

Pick the toolchain by the entry point, never by calling `full-build.sh` directly:

```bash
bash script/gcc_full_build.sh                 # output/full-gcc/{quic,storage,avb}
bash script/clang_full_build.sh               # output/full-clang/{quic,storage,avb}
```

Both forward every option to `script/full-build.sh`:

| Option | Meaning |
| :--- | :--- |
| `--config <name>` | Qbs configuration to build (default `debug`). |
| `--set <name>` | Build only this set; repeatable. Default: all of them. |
| `--list-sets` | Print the sets and the features they enable, then exit. |
| `--jobs <n>` | Parallel jobs (default `nproc`). |
| `--clean` | Delete each set's build root before building it. |
| `--keep-going` | Build the remaining sets after one fails, and fail at the end. |
| `--build-root <dir>` | Parent directory for the per-set build roots (same as `KMX_BUILD_ROOT`). |
| `--qbs-property <k:v>` | Extra property for every qbs invocation; repeatable. |

`QBS_PROFILE` overrides the profile the wrapper picked. The wrappers otherwise try their candidate
profiles in order and use the first one that resolves to an installed compiler, because
`script/qbs-profile.sh` prefers GCC — the right answer for the test runners and the wrong one for a
clang build.

```bash
bash script/gcc_full_build.sh --list-sets                  # what each set enables
bash script/clang_full_build.sh --set quic --clean         # one set, from scratch
bash script/gcc_full_build.sh --config release --jobs 8
QBS_PROFILE=clang20 bash script/clang_full_build.sh
```

A run prints the profile it settled on, one line per set as it starts, and a summary of every set with
its wall-clock time at the end.

## Typical Usage

Run all enabled dependency installers:

```bash
bash script/install-dependencies.sh
```

Run all enabled unit suites:

```bash
bash script/run-unit-tests.sh
```

Run all enabled integration suites on pre-built artifacts:

```bash
bash script/run-integration-tests.sh
```

Run the unit suites under sanitizers, or with coverage:

```bash
bash script/run-sanitizer-tests.sh asan+ubsan
bash script/run-coverage.sh
```

Enable selected features for script-driven workflows:

```bash
KMX_ENABLE_READINESS=true \
KMX_ENABLE_HTTP3=true \
KMX_ENABLE_MODBUS=true \
KMX_ENABLE_SOMEIP=true \
    bash script/run-unit-tests.sh
```
