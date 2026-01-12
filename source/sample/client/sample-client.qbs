import qbs

CppApplication {
    Depends
    {
        name: 'kmx-aio-lib'
    }

    name: "kmx-aio-sample-client"
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
        "inc/kmx/aio/sample/client/**.hpp",
        "src/kmx/aio/sample/client/**.cpp",
        "src/main.cpp",
    ]
}
