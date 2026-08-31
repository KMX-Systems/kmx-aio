import qbs

CppApplication {
    Depends { name: "kmx-aio-readiness" }

    name: "sample-tls-echo-readiness-server"
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
        "pthread",
    ].concat(project.tls_libraries)
    files: [
        "inc/kmx/aio/sample/tls/echo_readiness_server/**.hpp",
        "src/kmx/aio/sample/tls/echo_readiness_server/**.cpp",
        "src/main.cpp",
    ]
}
