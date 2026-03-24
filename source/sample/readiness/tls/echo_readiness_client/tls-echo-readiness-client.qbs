import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }
    Depends { name: "kmx-aio-sample-tcp-echo-common" }

    name: "kmx-aio-sample-tls-echo-readiness-client"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "../echo_readiness_server/inc",
        "../../tcp/echo/common/inc",
        "../../../../library/inc",
        "../../../../library/inc_dep"
    ]
    cpp.dynamicLibraries: ["ssl", "crypto"]
    files: [
        "inc/kmx/aio/sample/tls/echo_readiness_client/**.hpp",
        "src/kmx/aio/sample/tls/echo_readiness_client/**.cpp",
        "src/main.cpp",
    ]
}
