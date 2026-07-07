import qbs

CppApplication {
    Depends { name: "kmx-aio-completion" }
    Depends { name: "kmx-aio-avb" }

    name: "sample-avb-listener"
    condition: project.enable_avb
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "../common/inc",
        "api",
        "../../../../library/inc",
    ]
    cpp.dynamicLibraries: [
        "uring",
        "pthread"
    ]
    files: [
        "inc/kmx/aio/sample/avb/listener/**.hpp",
        "src/kmx/aio/sample/avb/listener/**.cpp",
        "src/main.cpp",
    ]
}
