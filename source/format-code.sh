#!/usr/bin/env bash
# Formats the project's own C and C++ sources with source/.clang-format.
#
# The file list comes from git, rooted at this script's directory, so the result is the same whichever
# directory the script is started from. A plain `find .` reformatted everything below the working
# directory - started from the repository root, that included the vendored dependency checkouts under
# output/ (BoringSSL, lsquic, SPDK and DPDK), whose rebuilds then failed on the reformatted sources.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

# Tracked files plus new ones git does not ignore; output/ is ignored. Files deleted from the working tree
# are still tracked until committed, and the samples' api/ directories are symlinks into the library, so
# only regular files that exist are passed on.
git ls-files -z --cached --others --exclude-standard -- '*.cpp' '*.hpp' '*.c' '*.h' |
    while IFS= read -r -d '' file; do
        if [[ -f "${file}" && ! -L "${file}" ]]; then
            printf '%s\0' "${file}"
        fi
    done |
    xargs -0 -r clang-format -i -style=file
