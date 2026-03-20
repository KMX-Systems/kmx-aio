import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "kmx-aio-sample-xdp-packet-filter"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "../../../../library/inc",
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
