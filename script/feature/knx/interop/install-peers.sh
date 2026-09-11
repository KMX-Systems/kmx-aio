#!/usr/bin/env bash
set -euo pipefail

# Installs the pinned interoperability peers under output/interop/, where the run-*-interop.sh scripts look for them:
#
#   xknx-venv/   xknx 3.20.0 in a Python virtual environment
#   jdk-21/      a JDK 21 - the one JAVA_HOME names when it is 21, otherwise Eclipse Temurin 21, downloaded
#   calimero/    calimero-core, calimero-device and calimero-server 3.0-M2, from Maven Central
#
#   bash script/feature/knx/interop/install-peers.sh
#
# Each peer already in place is left as it is. The versions are the ones the interoperability matrix records; change
# them only together with its rows.

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../../.." && pwd)"
interop="$repo_root/output/interop"
xknx_version="3.20.0"
calimero_version="3.0-M2"
maven="https://repo1.maven.org/maven2/io/calimero"

mkdir -p "$interop/captures"

if [[ ! -x "$interop/xknx-venv/bin/python" ]]; then
    echo "==> xknx $xknx_version"
    python3 -m venv "$interop/xknx-venv"
    "$interop/xknx-venv/bin/pip" install --quiet "xknx==$xknx_version"
fi
"$interop/xknx-venv/bin/python" -c "import xknx.__version__ as v; assert v.__version__ == '$xknx_version', v.__version__"

if [[ ! -x "$interop/jdk-21/bin/java" ]]; then
    if [[ -n "${JAVA_HOME:-}" ]] && "$JAVA_HOME/bin/java" -version 2>&1 | grep -q 'version "21'; then
        echo "==> JDK 21 from JAVA_HOME"
        ln -sfn "$JAVA_HOME" "$interop/jdk-21"
    else
        echo "==> Eclipse Temurin 21"
        mkdir -p "$interop/jdk-21"
        curl -fsSL "https://api.adoptium.net/v3/binary/latest/21/ga/linux/x64/jdk/hotspot/normal/eclipse" |
            tar -xz -C "$interop/jdk-21" --strip-components=1
    fi
fi
"$interop/jdk-21/bin/java" -version 2>&1 | grep -q 'version "21'

mkdir -p "$interop/calimero"
for artifact in calimero-core calimero-device calimero-server; do
    jar="$interop/calimero/$artifact-$calimero_version.jar"
    if [[ ! -s "$jar" ]]; then
        echo "==> $artifact $calimero_version"
        curl -fsSL -o "$jar" "$maven/$artifact/$calimero_version/$artifact-$calimero_version.jar"
    fi
done

echo "==> interoperability peers ready under $interop"
