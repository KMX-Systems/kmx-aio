import qbs

CppApplication {
    name: "http3-server"
    condition: project.enable_quic
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "api",
        "../../../../library/inc_dep",
    ]

    // Enable QUIC feature gate locally if needed, but the library should have it
    cpp.defines: ["KMX_AIO_FEATURE_QUIC=1"]

    Depends { name: "kmx-aio-lib" }

    files: [
        "main.cpp",
    ]
}
