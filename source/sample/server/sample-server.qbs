import qbs

CppApplication {
    Depends
    {
        name: 'kmx-aio-lib'
    }

    name: "kmx-aio-sample-server"
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
        "inc/kmx/aio/sample/server/**.hpp",
        "src/kmx/aio/sample/server/**.cpp",
        "src/main.cpp",
    ]
}
