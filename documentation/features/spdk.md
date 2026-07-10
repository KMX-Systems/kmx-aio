# SPDK

SPDK integration is completion-model only and feature-gated.

## Components

- `completion::spdk::runtime`
- `completion::spdk::device`

## Build Gate

- `project.enable_spdk:true` (default)

## Typical Dependencies

- `libaio-dev`
- `libnuma-dev`
- `uuid-dev`
- `meson`
- `ninja-build`

## C++ Key Methods

Minimal API flow used by the SPDK discovery/minimal samples:

```cpp
auto exec = std::make_shared<kmx::aio::completion::executor>();

// Runtime-level discovery
auto bdevs = kmx::aio::completion::spdk::runtime::enumerate_bdevs();

kmx::aio::completion::spdk::device_config cfg {
    .bdev_name = "Nvme0n1",
    .block_size = 4096u,
    .block_count = 128u,
};
auto dev_result = kmx::aio::completion::spdk::device::create(exec, cfg);
if (!dev_result)
    co_return;

auto dev = std::move(*dev_result);

co_await dev.write(0u, write_block);
co_await dev.read(0u, read_block);
co_await dev.flush();

kmx::aio::completion::spdk::runtime::finalize();
```

## Full C++ Sample Projects

Complete buildable samples are already available in the repository:

- `source/sample/completion/spdk/discovery/spdk-discovery.qbs`
- `source/sample/completion/spdk/discovery/src/main.cpp`
- `source/sample/completion/spdk/discovery/src/kmx/aio/sample/spdk/discovery/manager.cpp`
- `source/sample/completion/spdk/minimal/spdk-minimal.qbs`
- `source/sample/completion/spdk/minimal/src/main.cpp`
- `source/sample/completion/spdk/minimal/src/kmx/aio/sample/spdk/minimal/manager.cpp`

## Local Setup (Ubuntu/Debian)

Install prerequisites:

```bash
sudo apt update
sudo apt install -y \
    build-essential pkg-config meson ninja-build git \
    python3 python3-jinja2 python3-pyelftools python3-tabulate \
    libaio-dev libnuma-dev uuid-dev libssl-dev libelf-dev libpcap-dev
```

Workspace-local SPDK build (recommended for this repository):

```bash
git clone --depth 1 --branch v24.09 https://github.com/spdk/spdk.git build/spdk-local/src
git -C build/spdk-local/src submodule update --init --recursive
cd build/spdk-local/src
./configure --prefix="$PWD/../install-local" --with-shared \
    --disable-tests --disable-unit-tests --disable-apps --disable-examples
make -j"$(nproc)"
make install
```

Verify visible SPDK modules:

```bash
pkg-config --modversion spdk_nvme
pkg-config --modversion spdk_bdev
```

## Example

Run discovery first, then minimal sample:

```bash
DISCOVERY_BIN=$(find ./debug ./default -path "*/kmx-aio-sample-spdk-discovery" -type f 2>/dev/null | head -1)
[ -n "$DISCOVERY_BIN" ] || { echo "kmx-aio-sample-spdk-discovery not found"; exit 1; }
"$DISCOVERY_BIN"
"$DISCOVERY_BIN" Nvme0n1 Malloc0n1
SPDK_MIN_BIN="$(find debug -type f -name sample-spdk-minimal | head -n 1)"
"$SPDK_MIN_BIN" --help
```

## Troubleshooting

- `pkg-config` cannot find SPDK: export `PKG_CONFIG_PATH` to the local install path.
- Runtime bdevs missing: run discovery first, then pass an existing bdev name into the minimal sample.
- Hugepage-related runtime errors: configure hugepages and `/dev/hugepages` permissions per your host policy.
