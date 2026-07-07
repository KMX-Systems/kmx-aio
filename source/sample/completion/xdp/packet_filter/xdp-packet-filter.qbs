import qbs

CppApplication {
    condition: project.enable_af_xdp
    Depends { name: "kmx-aio-completion" }
    Depends { name: "kmx-aio-xdp" }

    name: "sample-xdp-packet-filter"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "api",
        "inc_dep",
        "../../../../library/inc_dep",
    ]
    cpp.dynamicLibraries: [
        "uring",
        "pthread"
    ]
    files: [
        "inc/kmx/aio/sample/xdp/packet_filter/**.hpp",
        "src/kmx/aio/sample/xdp/packet_filter/**.cpp",
        "src/main.cpp",
    ]
}
