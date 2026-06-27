import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "sample-v4l2-capture"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "api",
        "../../../../library/inc_dep"
    ]
    files: [
        "inc/kmx/aio/sample/v4l2/capture/**.hpp",
        "src/kmx/aio/sample/v4l2/capture/**.cpp",
        "src/main.cpp",
    ]
}
