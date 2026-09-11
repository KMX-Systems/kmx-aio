import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    // The generic AVB components are explicitly instantiated here for both execution models;
    // completion/avb and readiness/avb only alias those instantiations.
    Depends { name: "kmx-aio-completion"; condition: project.enable_completion }
    Depends { name: "kmx-aio-readiness" }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }

    name: "kmx-aio-avb"
    condition: project.enable_avb
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "../api",
        "../inc",
        "/usr/local/include",
    ]
    cpp.dynamicLibraries: [
        "pthread",
    ]
    install: true
    files: [
        "../api/kmx/aio/avb/**.hpp",
        "../inc/kmx/aio/avb/**.hpp",
        "../src/kmx/aio/avb/**.cpp",
        "../src/kmx/aio/avb/avtp/**.cpp",
        "../src/kmx/aio/avb/gptp/**.cpp",
        "../src/kmx/aio/avb/srp/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx-aio-completion"; condition: project.enable_completion }
        Depends { name: "kmx-aio-readiness" }
        Depends { name: "kmx_instrumentation" }
        Depends { name: "kmx_features" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}
