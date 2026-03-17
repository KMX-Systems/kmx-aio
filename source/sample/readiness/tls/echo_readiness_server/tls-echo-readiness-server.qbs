import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "kmx-aio-sample-tls-echo-readiness-server"
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
        "crypto",
        "ssl",
        "pthread"
    ]
    files: [
        "inc/kmx/aio/sample/tls/echo_readiness_server/**.hpp",
        "src/kmx/aio/sample/tls/echo_readiness_server/**.cpp",
        "src/main.cpp",
    ]
}
