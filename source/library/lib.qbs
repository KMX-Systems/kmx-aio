import qbs

StaticLibrary {
    Depends { name: "cpp" }
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "inc_dep",
    ]
    install: true
    name: "kmx-aio-lib"
    files: [
        "inc/kmx/**.hpp",
        "inc/kmx/aio/**.hpp",
        "inc/kmx/aio/tcp/**.hpp",
        "inc/kmx/aio/descriptor/**.hpp",
        "src/kmx/aio/**.cpp",
        "src/kmx/aio/tcp/**.cpp",
    ]
}
