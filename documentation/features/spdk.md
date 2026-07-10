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

## C++ Key Methods - Completion model

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

- [source/sample/completion/spdk/discovery/spdk-discovery.qbs](source/sample/completion/spdk/discovery/spdk-discovery.qbs)
- [source/sample/completion/spdk/discovery/src/main.cpp](source/sample/completion/spdk/discovery/src/main.cpp)
- [source/sample/completion/spdk/discovery/src/kmx/aio/sample/spdk/discovery/manager.cpp](source/sample/completion/spdk/discovery/src/kmx/aio/sample/spdk/discovery/manager.cpp)
- [source/sample/completion/spdk/minimal/spdk-minimal.qbs](source/sample/completion/spdk/minimal/spdk-minimal.qbs)
- [source/sample/completion/spdk/minimal/src/main.cpp](source/sample/completion/spdk/minimal/src/main.cpp)
- [source/sample/completion/spdk/minimal/src/kmx/aio/sample/spdk/minimal/manager.cpp](source/sample/completion/spdk/minimal/src/kmx/aio/sample/spdk/minimal/manager.cpp)

## Local Setup (Ubuntu/Debian)

Install prerequisites:

```bash
sudo apt update
sudo apt install -y \
    build-essential pkg-config meson ninja-build git \
    python3 python3-jinja2 python3-pyelftools python3-tabulate \
    libaio-dev libnuma-dev uuid-dev libssl-dev libelf-dev libpcap-dev
```

  Or run the repository bootstrap script:

  ```bash
  bash script/feature/spdk/install-dependencies.sh
  ```

  Or via umbrella bootstrap:

  ```bash
  bash script/bootstrap_optional_deps.sh --spdk
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

### General
- **`pkg-config` cannot find SPDK**: Export your `PKG_CONFIG_PATH` to point to `/usr/local/lib/pkgconfig` or your local workspace install path:
  ```bash
  export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
  ```
- **Runtime bdevs missing**: Run the discovery sample first with no arguments to list registered devices. If empty, ensure candidates (like NVMe drivers) are loaded or fallback malloc bdevs are active.

### Compiling SPDK From Source
- **Undefined `rpc_trace_*_ctx` structures in `trace_rpc.c`**: Clean stale generated files in your SPDK clone and rebuild:
  ```bash
  git reset --hard && git clean -xfd
  git submodule update --init --recursive
  ./configure --with-shared
  make -j"$(nproc)"
  ```
- **Implicit declaration of `OPENSSL_INIT_new` / `OPENSSL_INIT_free`**: A **BoringSSL** installation (for example under `/usr/local/include/openssl/`) is shadowing the system OpenSSL. Move BoringSSL files out of the way before building SPDK (BoringSSL is not tracked by any `.deb` package):
  ```bash
  sudo mv /usr/local/include/openssl /usr/local/include/openssl.boringssl.bak
  sudo mv /usr/local/lib/libssl.a    /usr/local/lib/libssl.a.boringssl.bak
  sudo mv /usr/local/lib/libcrypto.a /usr/local/lib/libcrypto.a.boringssl.bak
  ```
  Then perform a clean SPDK rebuild without a BoringSSL header conflict.

### Hugepage and Permissions Preparation
- **Allocate hugepages** (default is 1024 hugepages):
  ```bash
  echo 1024 | sudo tee /proc/sys/vm/nr_hugepages
  ```
- **Persist hugepages across reboot**:
  ```bash
  echo 'vm.nr_hugepages=1024' | sudo tee /etc/sysctl.d/99-spdk-hugepages.conf
  sudo sysctl --system
  ```
- **Permission Denied opening files under `/dev/hugepages`**: By default, SPDK requires write access to the hugetlbfs mount. Create the mount with explicit UID/GID ownership of your running user:
  ```bash
  sudo mkdir -p /dev/hugepages
  sudo mount -t hugetlbfs nodev /dev/hugepages -o uid=$(id -u),gid=$(id -g),mode=1770,pagesize=2M
  ```
- **Persist hugetlbfs mount in `/etc/fstab`**:
  ```fstab
  nodev /dev/hugepages hugetlbfs uid=1000,gid=1000,mode=1770,pagesize=2M 0 0
  ```
- **One-shot applying persistent state immediately**:
  ```bash
  echo 'vm.nr_hugepages=1024' | sudo tee /etc/sysctl.d/99-spdk-hugepages.conf
  grep -q '^nodev /dev/hugepages hugetlbfs ' /etc/fstab || \
      echo 'nodev /dev/hugepages hugetlbfs uid=1000,gid=1000,mode=1770,pagesize=2M 0 0' | sudo tee -a /etc/fstab
  sudo sysctl --system
  sudo mkdir -p /dev/hugepages
  sudo mount -a
  ```
- **Check hugepages reservation details**:
  ```bash
  cat /proc/sys/vm/nr_hugepages
  grep -i HugePages /proc/meminfo
  ls -ld /dev/hugepages
  mount | grep huge
  ```
