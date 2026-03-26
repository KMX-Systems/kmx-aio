import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "sample-xdp-packet-filter"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "api",
        "../../../../library/inc_dep",
    ]
    cpp.dynamicLibraries: [
        "uring",
        "pthread"
    ]
    files: [
        "src/main.cpp",
    ]
}
