import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "sample-tcp-minimal-server"
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
        "inc/kmx/aio/sample/tcp/minimal/server/**.hpp",
        "src/kmx/aio/sample/tcp/minimal/server/**.cpp",
        "src/main.cpp",
    ]
}
