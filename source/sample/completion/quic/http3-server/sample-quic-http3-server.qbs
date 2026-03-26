import qbs

CppApplication {
    name: "sample-quic-http3-server"
    condition: project.enable_quic
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "api",
        "inc_dep",
        "../../../../library/inc_dep",
    ]

    // Enable QUIC feature gate locally if needed, but the library should have it
    cpp.defines: ["KMX_AIO_FEATURE_QUIC=1"]

    Depends { name: "kmx-aio-lib" }

    files: [
        "inc/kmx/aio/sample/quic/http3_server/**.hpp",
        "src/kmx/aio/sample/quic/http3_server/**.cpp",
        "src/main.cpp",
    ]
}
