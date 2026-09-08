import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }

    name: "kmx-aio-knx"
    condition: project.enable_knx
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "../api",
        "../inc",
        "/usr/local/include",
    ]
    install: true
    files: [
        "../api/kmx/aio/knx/**.hpp",
        "../inc/kmx/aio/knx/**.hpp",
        "../src/kmx/aio/knx/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx_features" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}
