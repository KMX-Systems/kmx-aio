import qbs

CppApplication {
    name: "sample-gpu-image-processing"
    install: true
    condition: project.enable_cuda
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "api",
        "../../common/inc",
    ]
    Depends { name: "kmx-aio-sample-common" }

    Depends { name: "kmx-aio-completion" }
    Depends { name: "kmx-aio-gpu" }

    files: [
        "inc/kmx/aio/sample/gpu/image_processing/**.hpp",
        "src/kmx/aio/sample/gpu/image_processing/**.cpp",
        "src/main.cpp",
    ]
}
