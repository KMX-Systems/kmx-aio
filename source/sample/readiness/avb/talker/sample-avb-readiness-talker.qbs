import qbs

CppApplication {
    Depends { name: "kmx-aio-readiness" }
    Depends { name: "kmx-aio-avb" }

    name: "sample-avb-readiness-talker"
    condition: project.enable_avb
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "../../../completion/avb/common/inc",
        "../../api",
        "../../../../library/api",
        "../../../../library/inc",
    ]
    cpp.dynamicLibraries: [
        "pthread"
    ]
    files: [
        "inc/kmx/aio/sample/avb/talker/**.hpp",
        "src/kmx/aio/sample/avb/talker/**.cpp",
        "src/main.cpp",
    ]
}
