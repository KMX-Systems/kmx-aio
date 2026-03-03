import qbs

CppApplication {
    Depends
    {
        name: 'kmx-aio-lib'
    }

    name: "kmx-aio-sample-simple-udp-server"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "inc_dep"
    ]
    cpp.staticLibraries: [
    ]
    files: [
        "inc/kmx/aio/sample/simple_udp_server/**.hpp",
        "src/kmx/aio/sample/simple_udp_server/**.cpp",
        "src/main.cpp",
    ]
}
