import qbs

CppApplication {
    Depends { name: "kmx-aio-readiness" }
    Depends { name: "kmx-aio-avb" }
    Depends { name: "kmx-aio-sample-common" }

    name: "sample-avb-readiness-listener"
    condition: project.enable_avb
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "../../../common/inc",
        "../../../completion/avb/common/inc",
        "../../api",
        "../../../../library/api",
        "../../../../library/inc",
    ]
    cpp.dynamicLibraries: [
        "pthread"
    ]
    files: [
        "inc/kmx/aio/sample/avb/listener/**.hpp",
        "src/kmx/aio/sample/avb/listener/**.cpp",
        "src/main.cpp",
    ]
}
