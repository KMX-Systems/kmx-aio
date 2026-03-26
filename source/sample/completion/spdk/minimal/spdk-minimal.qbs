import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "sample-spdk-minimal"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.linkerFlags: [
        "-rpath",
        "/usr/local/lib",
    ]
    cpp.includePaths: [
        "inc",
        "api",
        "inc_dep",
        "../../../../library/inc_dep",
    ]
    cpp.dynamicLibraries: [
        "uring",
        "pthread"
    ]
    files: [
        "inc/kmx/aio/sample/spdk/minimal/**.hpp",
        "src/kmx/aio/sample/spdk/minimal/**.cpp",
        "src/main.cpp",
    ]
}
