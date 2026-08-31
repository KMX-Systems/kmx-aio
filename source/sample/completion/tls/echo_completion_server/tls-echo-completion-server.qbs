import qbs

CppApplication {
    Depends { name: "kmx-aio-completion" }

    name: "sample-tls-echo-completion-server"
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
        "pthread",
    ].concat(project.tls_libraries)
    files: [
        "inc/kmx/aio/sample/tls/echo_completion_server/**.hpp",
        "src/kmx/aio/sample/tls/echo_completion_server/**.cpp",
        "src/main.cpp",
    ]
}
