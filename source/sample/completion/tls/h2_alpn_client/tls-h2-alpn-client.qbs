import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }
    Depends { name: "kmx-aio-sample-tcp-echo-common" }

    name: "kmx-aio-sample-tls-h2-alpn-client"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "../../../readiness/tcp/echo/common/inc",
        "../../../../library/inc",
        "../../../../library/inc_dep"
    ]
    cpp.dynamicLibraries: ["ssl", "crypto", "uring"]
    files: [
        "inc/kmx/aio/sample/tls/h2_alpn_client/**.hpp",
        "src/kmx/aio/sample/tls/h2_alpn_client/**.cpp",
        "src/main.cpp",
    ]
}
