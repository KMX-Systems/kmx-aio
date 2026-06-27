import qbs

CppApplication {
    Depends { name: "kmx-aio-lib" }

    name: "sample-avb-talker"
    condition: project.enable_avb
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "../common/inc",
        "api",
        "../../../../library/api",
        "../../../../library/inc",
    ]
    cpp.dynamicLibraries: [
        "uring",
        "pthread"
    ]
    files: [
        "inc/kmx/aio/sample/avb/talker/**.hpp",
        "src/kmx/aio/sample/avb/talker/**.cpp",
        "src/main.cpp",
    ]
}
