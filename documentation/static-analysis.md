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

- `PROFILE=<qbs-profile>`
- `BUILD_DIR=<dir>` (default: `default`)
- `JOBS=<n>`
- `GCC_BIN=<path>`
- `GCC_TOOLCHAIN=<path>`

## Notes

- For clang-tidy compatibility, the helper normalizes `-std=c++26` to `-std=c++2c` inside generated compilation commands.
- The actual project build remains C++26 in QBS.

## Example

```bash
cd source
./clang-tidy.sh -checks='-*,clang-analyzer-*,bugprone-*' -header-filter='^.*/source/library/'
```
