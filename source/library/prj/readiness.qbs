import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx-aio-knx"; condition: project.enable_knx }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }

    name: "kmx-aio-readiness"
    condition: project.enable_readiness
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "../api",
        "../inc",
        "/usr/local/include",
    ].concat(project.quic_include_paths).concat(project.tls_include_paths)
    cpp.dynamicLibraries: [
        "pthread",
    ].concat(project.tls_libraries)
    install: true
    files: {
        var entries = [
            "../api/kmx/aio/readiness/**.hpp",
            "../api/kmx/aio/readiness/descriptor/**.hpp",
            "../api/kmx/aio/readiness/tcp/**.hpp",
            "../api/kmx/aio/readiness/udp/**.hpp",
            "../api/kmx/aio/readiness/knx/**.hpp",
            "../api/kmx/aio/readiness/tls/**.hpp",
            "../api/kmx/aio/readiness/v4l2/**.hpp",
            "../src/kmx/aio/readiness/executor.cpp",
            "../src/kmx/aio/readiness/statistics.cpp",
            "../src/kmx/aio/readiness/descriptor/**.cpp",
            "../src/kmx/aio/readiness/tcp/**.cpp",
            "../src/kmx/aio/readiness/udp/**.cpp",
            "../src/kmx/aio/readiness/tls/**.cpp",
            "../src/kmx/aio/readiness/v4l2/**.cpp",
            "../src/kmx/aio/readiness/openonload/**.cpp",
        ];

        if (project.enable_knx)
            entries.push("../src/kmx/aio/readiness/knx/**.cpp");

        if (project.enable_quic)
        {
            entries.push("../api/kmx/aio/readiness/quic/**.hpp");
            entries.push("../src/kmx/aio/quic/base_engine.cpp");
            entries.push("../src/kmx/aio/quic/generic_engine.cpp");
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
