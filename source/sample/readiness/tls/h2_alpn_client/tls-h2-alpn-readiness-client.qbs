import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "sample-tls-h2-alpn-readiness-client"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "../../../../library/inc",
        "../../../../library/inc_dep",
    ]
    cpp.dynamicLibraries: [
        "ssl",
        "crypto",
    ]
    files: [
        "inc/kmx/aio/sample/tls/h2_alpn_client/**.hpp",
        "src/kmx/aio/sample/tls/h2_alpn_client/**.cpp",
        "src/main.cpp",
    ]
}
