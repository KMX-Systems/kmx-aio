import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }

    name: "kmx-aio-http2"
    condition: project.enable_http2
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "../api",
        "/usr/local/include",
    ]
    install: true
    files: [
        "../api/kmx/aio/http2/**.hpp",
        "../src/kmx/aio/http2/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx_instrumentation" }
        Depends { name: "kmx_features" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}
