import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }

    name: "kmx-aio-http3"
    condition: project.enable_http3 && project.enable_quic
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "../api",
        "/usr/local/include",
    ]
    install: true
    files: [
        "../api/kmx/aio/http3/**.hpp",
        "../src/kmx/aio/http3/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}