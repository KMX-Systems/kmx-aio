import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "kmx-aio-sample-udp-minimal-client"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "inc_dep"
    ]
    files: [
        "inc/kmx/aio/sample/udp/minimal/client/**.hpp",
        "src/kmx/aio/sample/udp/minimal/client/**.cpp",
        "src/main.cpp",
    ]
}
