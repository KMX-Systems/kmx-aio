import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx-aio-completion" }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }

    name: "kmx-aio-xdp"
    condition: project.enable_af_xdp
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "../api",
        "/usr/local/include",
    ]
    cpp.dynamicLibraries: [
        "xdp",
        "bpf",
    ]
    install: true
    files: [
        "../api/kmx/aio/completion/xdp/**.hpp",
        "../src/kmx/aio/completion/xdp/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx-aio-completion" }
        Depends { name: "kmx_instrumentation" }
        Depends { name: "kmx_features" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}
