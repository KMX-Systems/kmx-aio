import qbs

CppApplication {
    name: "sample-someip-echo-client"
    condition: project.enable_someip
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "api",
        "inc_dep",
        "../../../../library/api",
        "../../../../library/inc_dep",
        project.someip_prefix ? project.someip_prefix + "/include" : "",
    ]

    cpp.defines: ["KMX_AIO_FEATURE_SOMEIP=1"]

    Depends { name: "kmx-aio-lib" }
    Depends { name: "kmx-aio-someip"; condition: project.enable_someip }

    files: [
        "src/main.cpp",
    ]
}
