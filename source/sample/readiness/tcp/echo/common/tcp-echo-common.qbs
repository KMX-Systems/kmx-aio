import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-lib" }

    name: "sample-tcp-echo-common"
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
        "inc/kmx/aio/sample/tcp/echo/**.hpp",
        "src/kmx/aio/sample/tcp/echo/**.cpp",
    ]
}
