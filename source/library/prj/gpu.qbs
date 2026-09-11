import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }

    name: "kmx-aio-gpu"
    condition: project.enable_cuda
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "../api",
        "../inc",
        "/usr/local/include",
    ]
    cpp.dynamicLibraries: [
        "cudart",
    ]
    install: true
    files: [
        "../api/kmx/aio/gpu/**.hpp",
        "../inc/kmx/aio/gpu/detail/**.hpp",
        "../src/kmx/aio/gpu/**.cpp",
        "../src/kmx/aio/gpu/detail/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx_instrumentation" }
        Depends { name: "kmx_features" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}
