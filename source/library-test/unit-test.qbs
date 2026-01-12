import qbs

CppApplication {
    Depends
    {
        name: 'kmx-aio-lib'
    }

    name: "kmx-aio-test"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "inc_dep"
    ]
    cpp.staticLibraries: [
        "Catch2Main",
        "Catch2"
    ]
    files: [
        "inc/kmx/aio/**.hpp",
        "src/kmx/aio/**.cpp",
    ]
}
