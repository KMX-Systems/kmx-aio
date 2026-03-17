import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    // We don't have a specific common library for UDP, so we'll just not depend on one if not needed.
    // Or we use tcp-echo-common for random buffers if needed.

    name: "kmx-aio-sample-udp-echo-uring-server"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "../../../../../library/inc",
        "../../../../../library/inc_dep",
    ]
    cpp.dynamicLibraries: [
        "uring",
        "pthread"
    ]
    files: [
        "inc/kmx/aio/sample/udp/echo_uring/server/**.hpp",
        "src/kmx/aio/sample/udp/echo_uring/server/**.cpp",
        "src/main.cpp",
    ]
}
