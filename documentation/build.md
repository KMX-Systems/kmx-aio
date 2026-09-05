# Build and Feature Gates

## Default Build

Current default active graph:

- `kmx-aio-core`
- `kmx-aio-completion`
- `kmx-aio-quic`

Everything else is disabled by default unless explicitly enabled.

```bash
qbs resolve -f source/source.qbs config:debug
qbs build -f source/source.qbs config:debug
```

This builds the test binary `kmx-aio-test` with only core, completion, and QUIC tests. Tests for readiness, AVB, OPC-UA, GPU, and other optional features are **not** included.

## Build All Tests

To build the complete test suite including tests for all optional features:

```bash
qbs resolve -f source/source.qbs config:debug project.full:true
qbs build -f source/source.qbs config:debug --products kmx-aio-test project.full:true
```

Or selectively enable only the features you need:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.enable_readiness:true \
    project.enable_avb:true

qbs build -f source/source.qbs config:debug --products kmx-aio-test \
    project.enable_readiness:true \
    project.enable_avb:true
```

## Baseline Build

```bash
qbs resolve -f source/source.qbs config:debug

qbs build -f source/source.qbs config:debug
```

## Enable Additional Project Sets

Readiness model:

```bash
qbs build -f source/source.qbs config:debug \
    project.enable_readiness:true
```

HTTP/2 stack:

```bash
qbs build -f source/source.qbs config:debug \
    project.enable_http2:true
```

HTTP/3 demo stack on top of QUIC:

```bash
qbs build -f source/source.qbs config:debug \
    project.enable_http3:true
```

Readiness + HTTP/3 together:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.enable_readiness:true \
    project.enable_http3:true

qbs build -f source/source.qbs config:debug \
    project.enable_readiness:true \
    project.enable_http3:true
```

## Build With All Features

To enable every optional feature gate in this repository, use one aggregate switch:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.full:true

qbs build -f source/source.qbs config:debug \
    project.full:true
```

`project.all:true` is accepted as an alias and behaves the same.

Note: `project.full:true` does not produce a binary that can be run. It puts QUIC next to SPDK and
OPC UA in one image: QUIC moves this project's TLS code to BoringSSL, while SPDK and open62541 are
prebuilt against the system OpenSSL and pull it into the same binary. Both export the same symbol names
for types laid out differently, so the link succeeds without a diagnostic and the result reads an
`SSL_CTX` at the wrong offsets at run time. `source/source.qbs` warns when it sees the combination.
Use it to check that everything compiles, and disable one side of the clash for anything you intend to
run — or use [the whole-tree build scripts](#whole-tree-build), which split the tree into feature sets
that are each internally consistent.

Note: aggregate flags are strict. With `project.full:true` (or `project.all:true`),
all optional gates are activated. During resolve/build, QBS runs
`script/bootstrap_optional_deps.sh` with the required feature flags so missing
third-party dependencies are downloaded/built/installed automatically.

### CI / Non-Interactive Environments

Automatic bootstrap may need package-manager access (`apt`, `dnf`, etc.) and
root privileges. In CI, use one of these patterns:

- Pre-provision a build image with required system packages.
- Provide passwordless sudo for the CI user.

Recommended preflight in CI:

```bash
sudo -n true
```

If this fails, dependency bootstrap cannot install missing system packages
non-interactively, and the resolve/build step will fail.

If you prefer an explicit template that you can tweak per gate, use:

```bash
qbs build -f source/source.qbs config:debug \
    project.enable_readiness:true \
    project.enable_http2:true \
    project.enable_quic:true \
    project.enable_http3:true \
    project.enable_modbus:true \
    project.enable_openonload:true \
    project.enable_af_xdp:true \
    project.enable_spdk:true \
    project.enable_avb:true \
    project.enable_opc_ua:true \
    project.enable_someip:true \
    project.enable_cuda:true
```

If you use a non-default SPDK or OPC UA install prefix, pass those as well:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.full:true \
    project.spdk_prefix:"$PWD/output/spdk-local/install-local" \
    project.opc_ua_vendored:true \
    project.opc_ua_prefix:"$PWD/output/open62541/install-local"

qbs build -f source/source.qbs config:debug \
    project.full:true \
    project.spdk_prefix:"$PWD/output/spdk-local/install-local" \
    project.opc_ua_vendored:true \
    project.opc_ua_prefix:"$PWD/output/open62541/install-local"
```

You can still disable any specific gate explicitly even with `project.full:true`, for example:

```bash
qbs build -f source/source.qbs config:debug \
    project.full:true \
    project.enable_spdk:false
```

## Common Feature Gates

```bash
# Example: disable SPDK and AF_XDP
qbs build -f source/source.qbs \
    project.enable_spdk:false \
    project.enable_af_xdp:false
```

## OPC-UA Local Install Build

If you installed OPC-UA with:

```bash
bash script/feature/opc_ua/install-dependencies.sh
```

then open62541 is installed under `output/open62541/install-local`.
The project default `project.opc_ua_prefix` now points to this local path.
Pass the prefix explicitly during resolve/build so headers like `open62541.h` are found:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.enable_opc_ua:true \
    project.opc_ua_prefix:"$PWD/output/open62541/install-local"

qbs build -f source/source.qbs config:debug \
    project.enable_opc_ua:true \
    project.opc_ua_prefix:"$PWD/output/open62541/install-local"
```

## SPDK Local Install Build

If you installed SPDK with:

```bash
bash script/feature/spdk/install-dependencies.sh
```

then SPDK is installed under `output/spdk-local/install-local`, not `/usr/local`.
The project default `project.spdk_prefix` now points to this local path.
Pass the prefix explicitly during resolve/build so headers like `spdk/bdev.h` are found:

```bash
qbs resolve -f source/source.qbs config:debug \
    project.enable_spdk:true \
    project.spdk_prefix:"$PWD/output/spdk-local/install-local"

qbs build -f source/source.qbs config:debug \
    project.enable_spdk:true \
    project.spdk_prefix:"$PWD/output/spdk-local/install-local"
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

### ISA-L

SPDK builds ISA-L from its own submodules whenever it finds nasm 2.14 or newer at configure time,
but it never links ISA-L into `libspdk_util.so` or `libspdk_accel.so` - those ship with the ISA-L
symbols undefined, so whoever links the shared SPDK libraries has to supply `-lisal` and
`-lisal_crypto`. What SPDK leaves in the install prefix is not dependable
([spdk#2736](https://github.com/spdk/spdk/issues/2736),
[spdk#3143](https://github.com/spdk/spdk/issues/3143)), and a prefix holding an ISA-L library that
resolves none of those symbols looks healthy until an unrelated product fails to link with
`undefined reference to 'isal_inflate'`.

[script/feature/spdk/install-isal.sh](../script/feature/spdk/install-isal.sh) settles that at
bootstrap time: it compares what the installed SPDK libraries still need against what the prefix
and the SPDK build tree actually define, copies ISA-L over from the build tree when the prefix
falls short, and fails with the offending symbol list rather than deferring the problem to a link.
`script/feature/spdk/install-dependencies.sh` runs it, and so does CI. Run it by hand after any
SPDK install done outside those scripts:

```bash
bash script/feature/spdk/install-isal.sh <spdk-source-dir> <spdk-install-prefix>
```

It also prints which file `-lisal` and `-lisal_crypto` resolve to and how many symbols each carries,
so an ISA-L link failure can be answered from the bootstrap log instead of guessed at.

Naming ISA-L on the link line is not by itself enough, and this is the part that bites. Nothing in a
program calls into `libspdk_util.so` or `libspdk_accel.so` directly - both arrive through
`libspdk_bdev.so`'s `DT_NEEDED`. Under `--as-needed`, which the GCC that Ubuntu ships enables by
default, the linker drops those two where they sit on the command line, reads past `-lisal` with
nothing left to resolve, and only pulls them back in once the archive is behind it; an archive is
never rescanned, so their ISA-L symbols end up undefined against a prefix that is perfectly healthy.
`kmx-aio-spdk` therefore exports `--no-as-needed`, which keeps every SPDK library at the point it is
named so ISA-L resolves it in place. Anything linking the shared SPDK libraries needs that flag,
whether or not it goes through this project's qbs files.

The build follows the prefix rather than assuming: `-lisal`/`-lisal_crypto` are named only when the
prefix holds them, so an SPDK built without nasm - and therefore without ISA-L - links just as well.

## Whole-Tree Build

Because no single set of gates compiles everything safely, a build that covers every translation unit is
several builds. `script/gcc_full_build.sh` and `script/clang_full_build.sh` run them:

```bash
bash script/full-build.sh              # default compiler -> output/full-default/{quic,storage,avb}
bash script/gcc_full_build.sh          # output/full-gcc/{quic,storage,avb}
bash script/clang_full_build.sh        # output/full-clang/{quic,storage,avb}
```

`script/full-build.sh` on its own builds with the machine's default compiler; the two wrappers ask for
one compiler family instead and keep their artifacts apart. All three build three feature sets — `quic` (BoringSSL side), `storage` (OpenSSL
side: SPDK, OPC UA, SOME/IP) and `avb` — into one build root each. `--list-sets`, `--set <name>`,
`--config`, `--clean` and `--keep-going` are the options you are likely to want; see
[Script Reference](scripts.md#whole-tree-builds) for the full list and for what each set contains.

## Collected Artifacts: install-root

Every build leaves its finished artifacts collected in one place, `<build root>/<config>/install-root`,
alongside the per-product directories qbs builds into:

```
output/debug/install-root/                     output/full-default/avb/release/install-root/
├── bin/      the samples and kmx-aio-test     ├── bin/      27 executables
├── lib/      libkmx-aio-*.a                   ├── lib/      7 static libraries
└── include/  the public headers, as a tree    └── include/  98 headers
```

This is qbs's own install step, which `qbs build` performs on its way out - there is no second command
to run, and `--no-install` skips it. What lands there is decided in the project files:

- `install: true` on a product installs its target: the static libraries into `lib/`, the sample
  executables and `kmx-aio-test` into `bin/`.
- The public headers are installed as a tree by a `Group` in `library/lib.qbs`, with
  `qbs.installSourceBase: "api"` stripping the `api/` prefix - so `api/kmx/aio/task.hpp` becomes
  `include/kmx/aio/task.hpp`, and an installed tree is compiled against with one
  `-I<install-root>/include`.
- `qbs.installPrefix` is emptied in the `kmx_instrumentation` module, which every product reaches. Qbs
  defaults it to `/usr/local`, which would bury everything under `install-root/usr/local/`.

Two things are deliberately left out. `kmx-aio-benchmark` is not installed - it is measured in place,
by `script/run-benchmarks.sh`. Neither are the sample support libraries (`kmx-aio-sample-common`,
`sample-tcp-echo-common`), which are build-internal glue rather than part of the library.

A whole-tree build has **one install-root per feature set**, not one for the tree:
`output/full-default/{quic,storage,avb}/release/install-root/`. They cannot be merged - the sets exist
precisely because their contents conflict, and `kmx-aio-test` is a different binary in each. Pick the
set whose features you want.

The binaries there are copies, not symlinks, and still need the compiler's own libstdc++ on
`LD_LIBRARY_PATH` like any other binary in the tree; see
[Toolchain Profile Used By The Scripts](#toolchain-profile-used-by-the-scripts).

`qbs install` re-runs just the install step against a build that is already there, and `--install-root`
puts the result outside the build tree:

```bash
cd source
qbs install -f source.qbs -d ../output config:release --install-root /tmp/kmx-aio-stage
```

## Toolchain Profile Used By The Scripts

The scripts under [script/](../script/) build with the machine's default C++ compiler - whatever `c++`
and `cc` resolve to, which on a Debian-family system is what `update-alternatives` points them at. No
compiler version is written down anywhere in them, so moving the alternative to another compiler is the
whole of what it takes to build with it.

A `qbs` command cannot be pointed at a bare compiler, only at a profile, and a command that names none
uses the machine-wide `defaultProfile` - a setting this repository does not control, which routinely
names a toolchain that was since renamed, removed or demoted. It then fails every build with `Could not
find selected C++ compiler` naming neither the profile nor the project, or, worse, quietly builds with a
compiler that is no longer the default. So [script/qbs-profile.sh](../script/qbs-profile.sh) finds or
makes a profile for the compiler that is actually wanted:

1. `QBS_PROFILE=<name>` when set, which wins over everything below;
2. otherwise `KMX_CXX` (default `c++`) with `KMX_CC` (default `cc`) beside it, for which the script
   keeps a profile of its own - `kmx-cxx` for the default `c++`, `kmx-gxx` for `g++`, and so on. It is
   created on first use and rewritten whenever the command starts resolving to a different toolchain.

Profiles that were not created this way are never adopted on their own, however well they match the
compiler: a profile is a bundle of build settings and not just a compiler path. `QBS_PROFILE` is how a
hand-made one gets used.

Each run prints the profile it settled on and the compiler behind it. To build with a different
toolchain:

```bash
KMX_CXX=g++ KMX_CC=gcc script/run-unit-tests.sh    # another compiler, profile handled for you
QBS_PROFILE=kmx-spdk-local script/run-unit-tests.sh # a profile you maintain yourself
```

Profiles themselves are inspected and repaired with `qbs config`:

```bash
qbs config --list profiles                       # everything configured
qbs config --list profiles.<name>                # one profile
qbs config --unset profiles.kmx-cxx              # forget a generated profile; the next build remakes it
qbs config defaultProfile <name>
```

One trap is worth knowing about, because the profiles above are written to avoid it. Clang picks C or
C++ driver mode from the name it was invoked under, so a profile has to reach it through a name
containing `++`. Point `cpp.cxxCompilerName` at the resolved `clang-24` binary instead and every C++
link fails with undefined references to `std::cout`, because a driver named `clang` links no C++
standard library. Note also that qbs joins `cpp.cxxCompilerName` onto `cpp.toolchainInstallPath` without
noticing an absolute name, so the name written there must be bare.

## Persistent QBS Profile For Local SPDK

To avoid repeating `project.spdk_prefix` on every command, create a dedicated profile once:

```bash
qbs config --add-profile kmx-spdk-local \
    project.enable_spdk true \
    project.spdk_prefix "$PWD/output/spdk-local/install-local"
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

Default gate state in [source/source.qbs](../source/source.qbs) (current project behavior):

- `project.full:false`
- `project.all:false`
- `project.enable_readiness:false`
- `project.enable_completion:true`
- `project.enable_http2:false`
- `project.enable_http3:false`
- `project.enable_openonload:false`
- `project.enable_af_xdp:false`
- `project.enable_spdk:false`
- `project.enable_quic:true`
- `project.enable_avb:false`
- `project.enable_opc_ua:false`
- `project.enable_modbus:false`
- `project.enable_someip:false`
- `project.enable_cuda:false`
- `project.enable_fault_injection:false`

## Instrumentation Gates

Sanitizers and coverage are project properties like the feature gates, and apply to every product -
libraries, samples and the test binary alike:

- `project.enable_asan:true` — AddressSanitizer
- `project.enable_ubsan:true` — UndefinedBehaviorSanitizer
- `project.enable_tsan:true` — ThreadSanitizer
- `project.enable_coverage:true` — gcov instrumentation (`--coverage`)
- `project.enable_fault_injection:true` — compiles the syscall seam's faulting policy in, so a test can
  make an individual system call fail on demand

ASan and TSan are mutually exclusive and the build says so rather than producing a binary that half
works; UBSan and coverage combine with anything.

The flags behind these switches live in the `kmx_instrumentation` QBS module under
`source/qbs/modules/`, found through the `qbsSearchPaths` set in `source/source.qbs`. Every product
depends on it and every library re-exports the dependency, which is what keeps a whole binary
consistent: a static library compiled with `-fsanitize=address` needs the executable that links it to
pull in the ASan runtime, and a coverage build that instruments only part of the tree leaves the rest
out of the report rather than showing it as uncovered.

`project.enable_fault_injection` is off by default and never wanted in a shipped build. The error
branches behind a failing `read`, `epoll_ctl` or `io_uring_submit` cannot be reached by calling the
public API, so `script/run-coverage.sh` turns it on and the tests drive them directly; `KMX_FAULT_INJECTION`
overrides that decision in either direction.

`script/run-sanitizer-tests.sh` and `script/run-coverage.sh` drive these builds and set the runtime
environment the resulting binaries need. See [Testing](testing.md) for both.

## Exported Feature Defines

When enabled, `kmx-aio-lib` exports these compile-time defines:

- `KMX_AIO_FEATURE_OPENONLOAD=1`
- `KMX_AIO_FEATURE_AF_XDP=1`
- `KMX_AIO_FEATURE_SPDK=1`
- `KMX_AIO_FEATURE_QUIC=1`
- `KMX_AIO_FEATURE_AVB=1`
- `KMX_AIO_FEATURE_OPC_UA=1` (only if OPC UA is enabled)
- `KMX_AIO_FEATURE_MODBUS=1`
- `KMX_AIO_FEATURE_SOMEIP=1`
- `KMX_AIO_FEATURE_CUDA=1`

The instrumentation gates export defines of their own, so code can tell how it was built:

- `KMX_AIO_SANITIZER_ASAN=1`
- `KMX_AIO_SANITIZER_UBSAN=1`
- `KMX_AIO_SANITIZER_TSAN=1`
- `KMX_AIO_COVERAGE=1`

If QBS reports profile/config mismatch, run `qbs resolve` first with the same file/profile/config values.

If you need clang-tidy integration, see [Static Analysis](static-analysis.md).
