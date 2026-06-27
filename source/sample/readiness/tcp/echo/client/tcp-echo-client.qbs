import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }
    Depends { name: "sample-tcp-echo-common" }

    name: "sample-tcp-echo-client"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "../common/inc",
        "api",
        "../../../../../library/inc_dep"
    ]
    files: [
        "inc/kmx/aio/sample/tcp/echo/client/**.hpp",
        "src/kmx/aio/sample/tcp/echo/client/**.cpp",
        "src/main.cpp",
    ]
}
