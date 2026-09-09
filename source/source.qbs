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
    property bool enable_modbus: full || all || false
    // The KNX secure and keyring surfaces are part of the KNX product and have no gate of their own.
    // They used to: enable_knx_secure and enable_knx_keyring defined KMX_AIO_FEATURE_KNX_SECURE and
    // KMX_AIO_FEATURE_KNX_KEYRING, which nothing anywhere read, while knx.qbs globbed secure.cpp and
    // keyring.cpp in unconditionally - so the two properties selected nothing and the release gate that
    // set them built the same binary as a plain enable_knx:true. A gate that compiles the same code
    // either way is worse than no gate: it reads as coverage of a configuration that was never built.
    property bool enable_knx: full || all || false
    property string opc_ua_prefix: sourceDirectory + "/../output/open62541/install-local"
    property bool enable_someip: full || all || false
    property bool someip_vendored: true
    property bool someip_link_backend: false
    property string someip_prefix: sourceDirectory + "/../output/someip/install-local"
    property bool enable_cuda: full || all || false  // GPU support (requires CUDA toolkit installed)

    // Every feature macro this build defines, named once.
    //
    // The macros used to be assembled by a copy of this list in each of the fifteen products that
    // needs them, and the copies had drifted: kmx-aio-http3 carried none at all, so <kmx/aio/quic/
    // engine.hpp> compiled to nothing inside the one library that exists to sit on top of QUIC;
    // kmx-aio-knx carried only the three KNX macros; and READINESS, COMPLETION, HTTP2 and HTTP3 were
    // defined by the benchmark and by nothing else, so the same header meant different things in the
    // benchmark and in the library it measures. A feature macro is only meaningful when a whole
    // binary agrees on it - the same argument the kmx_instrumentation module makes for the sanitizer
    // flags - so the list lives here, and reaches the compiler by two routes that both read it:
    //
    //   - the kmx_features module, which puts -D on the command line of every product depending on it;
    //   - <kmx/aio/config.hpp>, generated below, which is what carries the set out to code built
    //     against an installed tree, where none of this project's qbs files are in play.
    readonly property stringList feature_macros: {
        var macros = [];

        if (enable_readiness)
            macros.push("KMX_AIO_FEATURE_READINESS");
        if (enable_completion)
            macros.push("KMX_AIO_FEATURE_COMPLETION");
        if (enable_openonload)
            macros.push("KMX_AIO_FEATURE_OPENONLOAD");
        if (enable_af_xdp)
            macros.push("KMX_AIO_FEATURE_AF_XDP");
        if (enable_spdk)
            macros.push("KMX_AIO_FEATURE_SPDK");
        if (enable_quic)
            macros.push("KMX_AIO_FEATURE_QUIC");
        if (enable_http2)
            macros.push("KMX_AIO_FEATURE_HTTP2");
        if (enable_http3)
            macros.push("KMX_AIO_FEATURE_HTTP3");
        if (enable_avb)
            macros.push("KMX_AIO_FEATURE_AVB");
        if (enable_opc_ua)
            macros.push("KMX_AIO_FEATURE_OPC_UA");
        if (enable_modbus)
            macros.push("KMX_AIO_FEATURE_MODBUS");
        if (enable_knx)
            macros.push("KMX_AIO_FEATURE_KNX");
        if (enable_someip)
            macros.push("KMX_AIO_FEATURE_SOMEIP");
        if (enable_cuda)
            macros.push("KMX_AIO_FEATURE_CUDA");

        // Not a feature of its own: it selects whether the SOME/IP code talks to a real vsomeip
        // runtime or to the in-tree stand-in. It is here because <kmx/aio/someip/subscription.hpp>
        // changes shape with it, which makes it part of the installed API just as much as the rest.
        if (someip_link_backend)
            macros.push("KMX_AIO_SOMEIP_LINK_BACKEND");

        return macros;
    }

    // Writes <kmx/aio/config.hpp> - the installed record of feature_macros above - and hands back the
    // directory to put on the include path. Done from a probe, at resolve time, rather than from a
    // build rule: the header is included by public headers of every product, and a rule's output would
    // have to be sequenced ahead of each of their compile steps. Written before a single source file
    // is looked at, it needs no sequencing at all, and the compilation database picks it up too.
    Probe {
        id: featureConfigurationHeader

        // Inputs. Qbs re-runs a probe when one of its input properties changes and reuses the cached
        // result otherwise, so naming both of these is what makes a changed feature set - or a second
        // build directory - regenerate the header instead of inheriting the last one.
        property stringList macros: rootProject.feature_macros
        property string buildRoot: rootProject.buildDirectory

        // Outputs.
        property string includeDirectory
        property string headerPath

        configure: {
            includeDirectory = buildRoot + "/kmx-aio-config";

            var directory = includeDirectory + "/kmx/aio";
            File.makePath(directory);

            var path = directory + "/config.hpp";
            var lines = [
                "/// @file aio/config.hpp",
                "/// @brief The optional features this build of the library was compiled with.",
                "/// @details Generated during \"qbs resolve\" from the project.enable_* flags in",
                "///          source/source.qbs. Do not edit: every build overwrites it, and the flags",
                "///          there are what it is written from.",
                "///",
                "///          Every public header belonging to an optional feature includes this one and",
                "///          compiles to nothing when its feature is absent from the list below, so code",
                "///          built against an installed tree sees exactly the API that was built into it",
                "///          instead of declarations whose definitions were never compiled.",
                "/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.",
                "#pragma once",
                "",
            ];

            if (macros.length === 0) {
                lines.push("// This build enables no optional features.");
            } else {
                lines.push("// Each definition is conditional because the library's own translation units are compiled");
                lines.push("// with -D for this same set as well (the kmx_features module). Both come from the one list");
                lines.push("// in source/source.qbs and so cannot disagree; the guard is what keeps saying it twice from");
                lines.push("// being a redefinition.");

                for (var i = 0; i < macros.length; ++i) {
                    lines.push("#ifndef " + macros[i]);
                    lines.push("    #define " + macros[i] + " 1");
                    lines.push("#endif");
                }
            }

            var file = new TextFile(path, TextFile.WriteOnly);
            try {
                for (var line = 0; line < lines.length; ++line)
                    file.writeLine(lines[line]);
            } finally {
                file.close();
            }

            headerPath = path;
            found = true;
        }
    }

    // Where the generated <kmx/aio/config.hpp> lives. Reading it through the probe's output rather
    // than recomputing the path is deliberate: it is what makes the header exist before anything asks
    // for the directory it is in.
    readonly property string generated_include_dir: featureConfigurationHeader.includeDirectory

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
