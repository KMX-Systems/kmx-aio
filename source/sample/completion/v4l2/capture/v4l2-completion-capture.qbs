import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "sample-v4l2-completion-capture"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "api",
    ]
    cpp.dynamicLibraries: [
        "uring",
        "pthread"
    ]
    files: [
        "inc/kmx/aio/sample/v4l2/completion_capture/**.hpp",
        "src/kmx/aio/sample/v4l2/completion_capture/**.cpp",
        "src/main.cpp",
    ]
}
