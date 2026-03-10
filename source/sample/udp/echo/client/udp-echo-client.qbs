import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "kmx-aio-sample-udp-echo-client"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "inc_dep"
    ]
    files: [
        "inc/kmx/aio/sample/udp/echo/client/**.hpp",
        "src/kmx/aio/sample/udp/echo/client/**.cpp",
        "src/main.cpp",
    ]
}
