import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx-aio-knx"; condition: project.enable_knx }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }

    name: "kmx-aio-completion"
    condition: project.enable_completion
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
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
            "../api/kmx/aio/completion/knx/**.hpp",
            "../api/kmx/aio/completion/tls/**.hpp",
            "../api/kmx/aio/completion/v4l2/**.hpp",
            "../inc/kmx/aio/completion/**.hpp",
            "../src/kmx/aio/completion/detail/**.cpp",
            "../src/kmx/aio/completion/executor.cpp",
            "../src/kmx/aio/completion/statistics.cpp",
            "../src/kmx/aio/completion/timer.cpp",
            "../src/kmx/aio/completion/tcp/**.cpp",
            "../src/kmx/aio/completion/udp/**.cpp",
            "../src/kmx/aio/completion/tls/**.cpp",
            "../src/kmx/aio/completion/v4l2/**.cpp",
        ];

        if (project.enable_knx)
            entries.push("../src/kmx/aio/completion/knx/**.cpp");

        if (project.enable_quic)
        {
            entries.push("../src/kmx/aio/completion/quic/**.cpp");
            entries.push("../src/kmx/aio/quic/base_engine.cpp");
            entries.push("../src/kmx/aio/quic/primary_base_impl.cpp");
        }

        return entries;
    }

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx_instrumentation" }
        Depends { name: "kmx_features" }
        // The TLS streams are part of the exported API, so dependents include <openssl/ssl.h> through
        // it and must see the same implementation's headers this library was compiled against.
        cpp.includePaths: [ product.sourceDirectory + "/../api" ].concat(project.tls_include_paths)
    }
}
