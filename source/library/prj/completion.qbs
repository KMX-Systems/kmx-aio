import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx_instrumentation" }

    name: "kmx-aio-completion"
    condition: project.enable_completion
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
    cpp.dynamicLibraries: [
        "uring",
        "pthread",
    ].concat(project.tls_libraries)
    install: true
    files: {
        var entries = [
            "../api/kmx/aio/completion/**.hpp",
            "../api/kmx/aio/completion/tcp/**.hpp",
            "../api/kmx/aio/completion/udp/**.hpp",
            "../api/kmx/aio/completion/tls/**.hpp",
            "../api/kmx/aio/completion/v4l2/**.hpp",
            "../inc/kmx/aio/completion/**.hpp",
            "../src/kmx/aio/completion/detail/**.cpp",
            "../src/kmx/aio/completion/executor.cpp",
            "../src/kmx/aio/completion/timer.cpp",
            "../src/kmx/aio/completion/tcp/**.cpp",
            "../src/kmx/aio/completion/udp/**.cpp",
            "../src/kmx/aio/completion/tls/**.cpp",
            "../src/kmx/aio/completion/v4l2/**.cpp",
        ];

        if (project.enable_avb)
            entries.push("../src/kmx/aio/completion/avb/**.cpp");

        if (project.enable_quic)
        {
            entries.push("../src/kmx/aio/completion/quic/**.cpp");
            entries.push("../src/kmx/aio/quic/base_engine.cpp");
        }

        return entries;
    }

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx_instrumentation" }
        // The TLS streams are part of the exported API, so dependents include <openssl/ssl.h> through
        // it and must see the same implementation's headers this library was compiled against.
        cpp.includePaths: [ product.sourceDirectory + "/../api" ].concat(project.tls_include_paths)
    }
}
