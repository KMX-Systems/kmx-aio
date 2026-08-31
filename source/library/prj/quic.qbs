import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx_instrumentation" }

    name: "kmx-aio-quic"
    condition: project.enable_quic
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.defines: {
        var defs = [];
        if (project.enable_openonload)
            defs.push("KMX_AIO_FEATURE_OPENONLOAD=1");
        if (project.enable_af_xdp)
            defs.push("KMX_AIO_FEATURE_AF_XDP=1");
        if (project.enable_spdk)
            defs.push("KMX_AIO_FEATURE_SPDK=1");
        if (project.enable_quic)
            defs.push("KMX_AIO_FEATURE_QUIC=1");
        if (project.enable_avb)
            defs.push("KMX_AIO_FEATURE_AVB=1");
        if (project.enable_opc_ua)
            defs.push("KMX_AIO_FEATURE_OPC_UA=1");
        if (project.enable_cuda)
            defs.push("KMX_AIO_FEATURE_CUDA=1");
        return defs;
    }
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
            // The transport's only translation unit: the stream read/write bodies, plus server ALPN
            // selection, which is BoringSSL's job rather than lsquic's and which a server handshake
            // fails without.
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
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
                .concat(project.quic_include_paths).concat(project.tls_include_paths)
    }
}
