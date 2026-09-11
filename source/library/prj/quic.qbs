import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }

    name: "kmx-aio-quic"
    condition: project.enable_quic
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "../api",
        "../inc",
        "/usr/local/include",
    ].concat(project.quic_include_paths).concat(project.tls_include_paths)
    cpp.dynamicLibraries: project.quic_libraries
    install: true
    files: {
        var entries = [
            "../api/kmx/aio/quic/**.hpp",
            "../api/kmx/aio/completion/quic/**.hpp",
            "../inc/kmx/aio/quic/**.hpp",
            "../inc/kmx/aio/quic/detail/**.hpp",
            // The transport's translation units: the endpoint, byte buffer and stream bodies, plus server
            // ALPN selection, which is BoringSSL's job rather than lsquic's and which a server handshake
            // fails without.
            "../src/kmx/aio/quic/basic_endpoint.cpp",
            "../src/kmx/aio/quic/byte_buffer.cpp",
            "../src/kmx/aio/quic/stream.cpp",
            "../src/kmx/aio/quic/transport.cpp",
        ];

        if (project.enable_readiness)
            entries.push("../api/kmx/aio/readiness/quic/**.hpp");

        return entries;
    }

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx_instrumentation" }
        Depends { name: "kmx_features" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
                .concat(project.quic_include_paths).concat(project.tls_include_paths)
    }
}
