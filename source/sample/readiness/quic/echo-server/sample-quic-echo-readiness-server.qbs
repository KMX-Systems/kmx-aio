import qbs

CppApplication {
    name: "sample-quic-echo-readiness-server"
    condition: project.enable_quic
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "../../../../library/api",
        "../../../../library/inc_dep",
        "../../../../../build/lsquic/include",
    ]

    cpp.defines: ["KMX_AIO_FEATURE_QUIC=1"]

    Depends { name: "kmx-aio-lib" }

    files: [
        "inc/kmx/aio/sample/quic/echo_server/**.hpp",
        "src/kmx/aio/sample/quic/echo_server/**.cpp",
        "src/main.cpp",
    ]
}
