# Static Analysis (clang-tidy)

Run clang-tidy through the helper script in `source/`:

```bash
cd source
./clang-tidy.sh
```

The helper script:

- generates `compile_commands.json` via `qbs generate -g clangdb`
- runs `run-clang-tidy` against that database

## Optional Environment Variables

- `PROFILE=<qbs-profile>` — passed to `qbs generate` as `profile:<name>`; the qbs default profile otherwise
- `BUILD_DIR=<dir>` (default: `output/clangdb`, relative to `source/`)
- `PROJECT_FILE=<file>` (default: `source.qbs`)
- `JOBS=<n>` (default: `nproc`)
- `GCC_BIN=<path>` (default: `/usr/bin/g++`)
- `GCC_TOOLCHAIN=<path>`

## Notes

- For clang-tidy compatibility, the helper normalizes `-std=c++26` to `-std=c++2c` inside generated compilation commands.
- The actual project build remains C++26 in QBS.
- Extra CLI arguments are forwarded to `run-clang-tidy`, not to `qbs generate` — use them for
  `-checks`, `-header-filter` and the like.
- The database only covers what the current feature gates compile. The script runs `qbs generate` with
  no feature properties, so the default gates apply and the optional features never reach
  `compile_commands.json`. To widen the coverage, generate the database yourself with the gates you
  want and point `run-clang-tidy` at it:

```bash
cd source
qbs generate -f source.qbs -d output/clangdb-full -g clangdb project.full:true
run-clang-tidy -p output/clangdb-full
```

  Note that `project.full:true` combines gates that cannot share a binary; that does not matter for a
  compilation database, which records compile commands and never links. For a per-feature-set database,
  generate one per set as [the whole-tree build](build.md#whole-tree-build) does.

## Example

```bash
cd source
./clang-tidy.sh -checks='-*,clang-analyzer-*,bugprone-*' -header-filter='^.*/source/library/'
```
