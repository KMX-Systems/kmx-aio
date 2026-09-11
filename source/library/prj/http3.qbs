import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }

    name: "kmx-aio-http3"
    condition: project.enable_http3 && project.enable_quic
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "../api",
        "../inc",
        "/usr/local/include",
    ]
    install: true
    // A qbs "**.cpp" matches only the named directory, not the ones below it, so each subdirectory is listed.
    files: [
        "../api/kmx/aio/http3/**.hpp",
        "../api/kmx/aio/http3/demo/**.hpp",
        "../api/kmx/aio/http3/qpack/**.hpp",
        "../inc/kmx/aio/http3/detail/**.hpp",
        "../src/kmx/aio/http3/**.cpp",
        "../src/kmx/aio/http3/demo/**.cpp",
        "../src/kmx/aio/http3/detail/**.cpp",
        "../src/kmx/aio/http3/qpack/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx_instrumentation" }
        Depends { name: "kmx_features" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}