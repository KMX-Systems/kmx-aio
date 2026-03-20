import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "kmx-aio-sample-spdk-minimal"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.linkerFlags: [
        "-rpath",
        "/usr/local/lib",
    ]
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
