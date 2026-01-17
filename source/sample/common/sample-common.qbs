import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends
    {
        name: 'kmx-aio-lib'
    }

    name: "kmx-aio-sample-common"
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
        "inc/kmx/aio/sample/**.hpp",
        "src/kmx/aio/sample/**.cpp",
    ]
}
