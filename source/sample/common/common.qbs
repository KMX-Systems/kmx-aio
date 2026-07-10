import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }

    name: "kmx-aio-sample-common"
    condition: project.enable_completion || project.enable_readiness || project.enable_cuda
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
    ]
    files: [
        "inc/kmx/aio/sample/common/**.hpp",
        "src/kmx/aio/sample/common/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        cpp.includePaths: [ product.sourceDirectory + "/inc" ]
    }
}
