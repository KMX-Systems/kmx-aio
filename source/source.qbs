import qbs 1.0
import qbs.Process

Project {
    id: rootProject
    // Core remains always active; all other products are feature-gated here.
    // Aggregate toggles for enabling all optional feature gates at once.
    property bool full: false
    property bool all: false

    property bool enable_readiness: full || all || false
    property bool enable_completion: true
    property bool enable_http2: full || all || false
    property bool enable_http3: full || all || false
    property bool enable_openonload: full || all || false
    property bool enable_af_xdp: full || all || false
    property bool enable_spdk: full || all || false
    property string spdk_prefix: sourceDirectory + "/../build/spdk-local/install-local"
    property bool spdk_enable_crypto: false
    property bool enable_quic: full || all || enable_http3 || false
    property bool enable_avb: full || all || false
    property bool enable_opc_ua: full || all || false
    property bool opc_ua_vendored: true
    property bool enable_modbus: false
    property string opc_ua_prefix: sourceDirectory + "/../build/open62541/install-local"
    property bool enable_someip: full || all || false
    property bool someip_vendored: true
    property bool someip_link_backend: false
    property string someip_prefix: sourceDirectory + "/../build/someip/install-local"
    property bool enable_cuda: full || all || false  // GPU support (requires CUDA toolkit installed)
    property bool enable_asan: false
    property bool enable_tsan: false

    Probe {
        id: dependencyBootstrap
        condition: rootProject.enable_af_xdp || rootProject.enable_avb || rootProject.enable_spdk ||
                   rootProject.enable_quic || rootProject.enable_http3 || rootProject.enable_opc_ua ||
                   rootProject.enable_someip || rootProject.enable_cuda
        property bool found: false

        configure: {
            // Use an absolute path: qbs may run this probe with a current working
            // directory other than the repository root (e.g. when a feature script
            // invokes "qbs resolve" from the source/ subdirectory), and a
            // cwd-relative path would then fail to resolve.
            var bootstrapScript = rootProject.sourceDirectory + "/../script/bootstrap_optional_deps.sh";
            var args = [bootstrapScript];

            if (rootProject.enable_af_xdp)
                args.push("--af-xdp");
            if (rootProject.enable_avb)
                args.push("--avb");
            if (rootProject.enable_spdk)
                args.push("--spdk");
            if (rootProject.enable_quic || rootProject.enable_http3)
                args.push("--quic");
            if (rootProject.enable_opc_ua)
                args.push("--opc-ua");
            if (rootProject.enable_someip)
                args.push("--someip");
            if (rootProject.enable_cuda)
                args.push("--accelerators");

            var p = new Process();
            var rc = p.exec("bash", args, true);
            p.close();

            if (rc !== 0)
                throw "Dependency bootstrap failed (exit code " + rc + ")";

            found = true;
        }
    }

    references: [
        "library/library.qbs",
        "library-test/unit-test.qbs",
        "sample/sample.qbs",
    ]
}
