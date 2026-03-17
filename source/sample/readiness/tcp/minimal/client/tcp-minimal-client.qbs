import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "kmx-aio-sample-tcp-minimal-client"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "../../../../../library/inc",
        "../../../../../library/inc_dep"
    ]
    files: [
        "inc/kmx/aio/sample/tcp/minimal/client/**.hpp",
        "src/kmx/aio/sample/tcp/minimal/client/**.cpp",
        "src/main.cpp",
    ]
}
