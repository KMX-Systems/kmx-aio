import qbs 1.0
import qbs.Environment
import qbs.File
import qbs.Process
import qbs.TextFile

Project {
    id: rootProject

    // Where the project's own qbs modules live; kmx_instrumentation, which carries the sanitizer and
    // coverage flags, is found through this. Set here rather than in kmx-aio.qbs because the build
    // scripts resolve source.qbs directly, making it the top-level project of most builds.
    qbsSearchPaths: [sourceDirectory + "/qbs"]

    // Core remains always active; all other products are feature-gated here.
    // Aggregate toggles for enabling all optional feature gates at once.
    property bool full: false
    property bool all: false

    // Compiles the syscall seam's faulting policy in, so tests can make a system call fail on demand.
    // Off by default and never wanted in a shipped build; script/run-coverage.sh turns it on, because
    // the branches that handle a failing syscall are otherwise unreachable from a test.
    property bool enable_fault_injection: false

    property bool enable_readiness: full || all || false
    property bool enable_completion: true
    property bool enable_http2: full || all || false
    property bool enable_http3: full || all || false
    property bool enable_openonload: full || all || false
    property bool enable_af_xdp: full || all || false
    property bool enable_spdk: full || all || false
    property string spdk_prefix: sourceDirectory + "/../output/spdk-local/install-local"
    property bool spdk_enable_crypto: false
    property bool enable_quic: full || all || enable_http3 || false
    property bool enable_avb: full || all || false
    property bool enable_opc_ua: full || all || false
    property bool opc_ua_vendored: true
    property bool enable_modbus: false
    property string opc_ua_prefix: sourceDirectory + "/../output/open62541/install-local"
    property bool enable_someip: full || all || false
    property bool someip_vendored: true
    property bool someip_link_backend: false
    property string someip_prefix: sourceDirectory + "/../output/someip/install-local"
    property bool enable_cuda: full || all || false  // GPU support (requires CUDA toolkit installed)

    // Instrumentation, applied to every product through the kmx_instrumentation module. ASan and TSan
    // are mutually exclusive; UBSan combines with either, and coverage combines with all of them.
    property bool enable_asan: false
    property bool enable_ubsan: false
    property bool enable_tsan: false
    property bool enable_coverage: false

    Probe {
        id: dependencyBootstrap
        condition: rootProject.enable_af_xdp || rootProject.enable_avb || rootProject.enable_spdk ||
                   rootProject.enable_quic || rootProject.enable_http3 || rootProject.enable_opc_ua ||
                   rootProject.enable_someip || rootProject.enable_cuda
        property bool found: false

        // Where the QUIC/TLS bootstrap records its decision, and the environment that steers it. Naming
        // the prefixes here is what makes a changed BORINGSSL_PREFIX re-run this probe: qbs reuses a
        // probe's cached result until one of its input properties changes.
        property string quicDependenciesFile: rootProject.quic_dependencies_file
        property string boringsslPrefix: Environment.getEnv("BORINGSSL_PREFIX") || ""
        property string lsquicPrefix: Environment.getEnv("LSQUIC_PREFIX") || ""
        property string forceVendoredQuic: Environment.getEnv("KMX_QUIC_FORCE_VENDORED") || ""

        // Filled in from quicDependenciesFile once the bootstrap above has written it.
        property var boringssl
        property var lsquic

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

            // script/feature/quic/install-dependencies.sh has just decided, per library, between an
            // already installed BoringSSL/lsquic and the pinned versions it builds under output/. Read
            // that decision here rather than searching again, so compiling and linking cannot disagree
            // with what was actually installed.
            if (File.exists(quicDependenciesFile)) {
                var configurationFile = new TextFile(quicDependenciesFile, TextFile.ReadOnly);
                var contents = configurationFile.readAll();
                configurationFile.close();

                var described = JSON.parse(contents);
                boringssl = described.boringssl;
                lsquic = described.lsquic;
            }

            // The one place where a single TLS implementation per binary cannot be enforced from here:
            // SPDK and open62541 are prebuilt against the system OpenSSL and carry it in as a transitive
            // dependency, so enabling them next to QUIC puts both implementations in the same link.
            if ((rootProject.enable_quic || rootProject.enable_http3) &&
                    (rootProject.enable_spdk || rootProject.enable_opc_ua)) {
                console.warn("QUIC moves this project's TLS code to BoringSSL, but SPDK and open62541 "
                             + "are linked against the system OpenSSL and pull it into the same binary. "
                             + "Rebuild those dependencies against BoringSSL, or keep the QUIC and "
                             + "SPDK/OPC UA features in separate executables.");
            }

            found = true;
        }
    }

    // Where the QUIC/TLS dependencies ended up. script/feature/quic/install-dependencies.sh decides, per
    // library, whether an already installed BoringSSL/lsquic is recent enough to use or whether the pinned
    // version has to be downloaded and built under output/, and records that decision here. The build reads
    // it instead of repeating the search, so compiling and linking always follow the same choice.
    property string quic_dependencies_file: sourceDirectory + "/../output/quic-dependencies.json"

    readonly property var quic_dependencies: {
        // What the bootstrap produces when it builds the pinned versions itself, and the answer for a
        // tree where it has not run at all - which keeps a bare "qbs build" working.
        var outputDirectory = sourceDirectory + "/../output";

        return {
            "boringssl": dependencyBootstrap.boringssl || {
                "origin": "vendored",
                "include_dir": outputDirectory + "/boringssl/include",
                "ssl_library": outputDirectory + "/boringssl/build/libssl.a",
                "crypto_library": outputDirectory + "/boringssl/build/libcrypto.a"
            },
            "lsquic": dependencyBootstrap.lsquic || {
                "origin": "vendored",
                "include_dir": outputDirectory + "/lsquic/include",
                "library": outputDirectory + "/lsquic/build/src/liblsquic/liblsquic.a"
            }
        };
    }

    // Header search path for anything that includes <lsquic.h>. An installed lsquic under /usr or
    // /usr/local needs no -I of its own, but naming it costs nothing and keeps a custom prefix working.
    readonly property stringList quic_include_paths: enable_quic
            ? [quic_dependencies.lsquic.include_dir]
            : []

    // Which TLS implementation the whole build speaks.
    //
    // OpenSSL and BoringSSL export the same symbol names for types that are laid out differently, so a
    // binary that compiles one translation unit against one set of headers and links the other gets no
    // diagnostic - just an SSL_CTX whose fields are read at the wrong offsets. Every product here has to
    // agree, headers and libraries alike. QUIC settles the choice when it is enabled: lsquic's backend
    // and the ALPN selection callback are BoringSSL-only, so the TLS code moves to BoringSSL with it. A
    // build without QUIC has no BoringSSL to speak of and stays on the system OpenSSL.
    property string tls_backend: enable_quic ? "boringssl" : "openssl"

    readonly property stringList tls_include_paths: {
        if (tls_backend !== "boringssl")
            return [];

        var directory = quic_dependencies.boringssl.include_dir;

        // -I/usr/include would move the compiler's own standard headers down the search order, and a
        // BoringSSL installed there is on the default search path anyway.
        if (directory === "/usr/include")
            return [];

        return [directory];
    }

    readonly property stringList tls_libraries: tls_backend === "boringssl"
            ? [
                quic_dependencies.boringssl.ssl_library,
                quic_dependencies.boringssl.crypto_library,
            ]
            : ["ssl", "crypto"]

    // Link order matters: lsquic pulls its crypto primitives out of BoringSSL, and BoringSSL's libssl
    // depends on libcrypto, so each entry has to come before the one that satisfies it.
    readonly property stringList quic_libraries: enable_quic
            ? [quic_dependencies.lsquic.library].concat(tls_libraries).concat(["z"])
            : []

    references: [
        "library/library.qbs",
        "library-benchmark/benchmark.qbs",
        "library-test/unit-test.qbs",
        "sample/sample.qbs",
    ]
}
