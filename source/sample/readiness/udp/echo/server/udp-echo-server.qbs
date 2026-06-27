import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "sample-udp-echo-server"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "api",
        "../../../../../library/inc_dep"
    ]
    files: [
        "inc/kmx/aio/sample/udp/echo/server/**.hpp",
        "src/kmx/aio/sample/udp/echo/server/**.cpp",
        "src/main.cpp",
    ]
}
