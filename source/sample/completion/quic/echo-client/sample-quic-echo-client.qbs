import qbs

CppApplication {
    name: "sample-quic-echo-client"
    condition: project.enable_quic
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "api",
        "inc_dep",
        "../../../../library/api",
        "../../../../library/inc_dep",
        "../../../../../build/lsquic/include",
    ]

    cpp.defines: ["KMX_AIO_FEATURE_QUIC=1"]

    Depends { name: "kmx-aio-lib" }

    files: [
        "inc/kmx/aio/sample/quic/echo_client/**.hpp",
        "src/kmx/aio/sample/quic/echo_client/**.cpp",
        "src/main.cpp",
    ]
}
