import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }
    Depends { name: "kmx-aio-sample-tcp-echo-common" }

    name: "kmx-aio-sample-tcp-echo-uring-server"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "../../../../readiness/tcp/echo/server/inc",
        "../../../../readiness/tcp/echo/common/inc",
        "../../../../../library/inc",
        "../../../../../library/inc_dep",
    ]
    cpp.dynamicLibraries: [
        "uring",
        "crypto",
        "ssl",
        "pthread"
    ]
    files: [
        "inc/kmx/aio/sample/tcp/echo_uring/server/**.hpp",
        "src/kmx/aio/sample/tcp/echo_uring/server/**.cpp",
        "src/main.cpp",
    ]
}
