import qbs

CppApplication {
    name: "hft-order-router"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "api",
        "../../../../library/inc_dep",
    ]

    Depends { name: "kmx-aio-lib" }

    files: [
        "src/main.cpp",
    ]
}
