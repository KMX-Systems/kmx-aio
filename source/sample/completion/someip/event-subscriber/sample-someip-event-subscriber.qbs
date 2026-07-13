import qbs

CppApplication {
    name: "sample-someip-event-subscriber"
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

    Depends { name: "kmx-aio-completion" }
    Depends { name: "kmx-aio-someip"; condition: project.enable_someip }

    files: [
        "inc/kmx/aio/sample/someip/event_subscriber/**.hpp",
        "src/kmx/aio/sample/someip/event_subscriber/**.cpp",
        "src/main.cpp",
    ]
}
