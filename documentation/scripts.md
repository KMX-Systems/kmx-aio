# Script Reference

This document is the authoritative reference for repository automation scripts used to install dependencies and run tests.

## Global Orchestration Scripts

These are the top-level scripts requested for full documentation.

| Script | Purpose | Behavior |
| :--- | :--- | :--- |
| `script/install-dependencies.sh` | Global dependency orchestrator | Sources `script/feature/common.sh`, iterates `feature_list`, and runs each feature `install-dependencies.sh` only when the feature is enabled via `KMX_ENABLE_<FEATURE>` or default state. |
| `script/run-unit-tests.sh` | Global unit-test orchestrator | Resolves/builds `source/source.qbs` in `config:debug` using feature args from `build_qbs_feature_args`, then runs enabled feature `run-unit-tests.sh` scripts. |
| `script/run-integration-tests.sh` | Global integration-test orchestrator | Runs enabled feature `run-integration-tests.sh` scripts against pre-built binaries. It auto-detects active features from `kmx-aio-test --list-tags` when `KMX_ENABLE_*` vars are not explicitly set. |

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
| `opc_ua` | `script/feature/opc_ua/install-dependencies.sh` | `script/feature/opc_ua/run-unit-tests.sh` (`[opc_ua]~[integration]`) | `script/feature/opc_ua/run-integration-tests.sh` (`[opc_ua][integration]`) | Bootstraps local `open62541` into `build/open62541/install-local`. |
| `openonload` | `script/feature/openonload/install-dependencies.sh` | `script/feature/openonload/run-unit-tests.sh` (`[openonload]~[integration]`) | `script/feature/openonload/run-integration-tests.sh` (`[openonload][integration]`) | Verifies OpenOnload prerequisites and host support state. |
| `quic` | `script/feature/quic/install-dependencies.sh` | `script/feature/quic/run-unit-tests.sh` | `script/feature/quic/run-integration-tests.sh` (`[quic][readiness][integration][smoke][slow]` and `[quic][http3][readiness][integration][smoke][slow]`) | Unit script currently prints no dedicated QUIC unit tests. Install script bootstraps BoringSSL + lsquic artifacts. |
| `readiness` | `script/feature/readiness/install-dependencies.sh` | `script/feature/readiness/run-unit-tests.sh` (`[readiness]~[integration]`) | `script/feature/readiness/run-integration-tests.sh` (`[readiness][integration]`) | No additional dependency install step required. |
| `someip` | `script/feature/someip/install-dependencies.sh` | `script/feature/someip/run-unit-tests.sh` (`[someip]~[integration]`) | `script/feature/someip/run-integration-tests.sh` (`[someip][integration]`) | Unit/integration scripts perform SOME/IP-enabled `qbs resolve`/`qbs build` before tests. Installer can prompt to install missing distro packages and builds local vsomeip prefix. |
| `spdk` | `script/feature/spdk/install-dependencies.sh` | `script/feature/spdk/run-unit-tests.sh` (`[spdk]~[integration]`) | `script/feature/spdk/run-integration-tests.sh` (`[spdk][integration]`) | Installs build deps and bootstraps local SPDK under `build/spdk-local/install-local`. |
| `v4l2` | `script/feature/v4l2/install-dependencies.sh` | `script/feature/v4l2/run-unit-tests.sh` (`[v4l2][model]`) | `script/feature/v4l2/run-integration-tests.sh` | Integration script currently reports no dedicated V4L2 integration tests. |

## Additional Feature-Specific Utilities

| Script | Purpose |
| :--- | :--- |
| `script/feature/cuda/check_env.sh` | Non-invasive environment check for CUDA runtime/toolkit readiness. |
| `script/feature/someip/run-smoke.sh` | End-to-end SOME/IP smoke runner with optional `--skip-build` and `--skip-samples`. Builds SOME/IP targets, runs `[someip]`, then executes sample binaries/log checks. |

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

Enable selected features for script-driven workflows:

```bash
KMX_ENABLE_READINESS=true \
KMX_ENABLE_HTTP3=true \
KMX_ENABLE_MODBUS=true \
KMX_ENABLE_SOMEIP=true \
    bash script/run-unit-tests.sh
```
