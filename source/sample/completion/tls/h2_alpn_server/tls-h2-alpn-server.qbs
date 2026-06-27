import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "sample-tls-h2-alpn-server"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "api",
        "../../../../library/inc_dep",
    ]
    cpp.dynamicLibraries: [
        "uring",
        "crypto",
        "ssl",
        "pthread"
    ]
    files: [
        "inc/kmx/aio/sample/tls/h2_alpn_server/**.hpp",
        "src/kmx/aio/sample/tls/h2_alpn_server/**.cpp",
        "src/main.cpp",
    ]
}
