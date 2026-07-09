import qbs

CppApplication {
    name: "sample-gpu-image-processing"
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
    cpp.defines: ["KMX_AIO_FEATURE_CUDA=1"]

    Depends { name: "kmx-aio-completion" }
    Depends { name: "kmx-aio-gpu" }

    files: [
        "inc/kmx/aio/sample/gpu/image_processing/**.hpp",
        "src/kmx/aio/sample/gpu/image_processing/**.cpp",
        "src/main.cpp",
    ]
}
