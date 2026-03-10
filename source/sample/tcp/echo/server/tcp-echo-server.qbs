import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }
    Depends { name: "kmx-aio-sample-tcp-echo-common" }

    name: "kmx-aio-sample-tcp-echo-server"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "inc_dep",
        "inc_dep2",
    ]
    files: [
        "inc/kmx/aio/sample/tcp/echo/server/**.hpp",
        "src/kmx/aio/sample/tcp/echo/server/**.cpp",
        "src/main.cpp",
    ]
}
