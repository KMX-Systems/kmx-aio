import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }
    Depends { name: "kmx-aio-sample-tcp-echo-common" }

    name: "kmx-aio-sample-tcp-echo-server"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "../common/inc",
        "../../../../../library/inc",
        "../../../../../library/inc_dep",
    ]
    files: [
        "inc/kmx/aio/sample/tcp/echo/server/**.hpp",
        "src/kmx/aio/sample/tcp/echo/server/**.cpp",
        "src/main.cpp",
    ]
}
