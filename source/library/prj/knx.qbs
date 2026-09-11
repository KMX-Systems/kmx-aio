import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }

    name: "kmx-aio-knx"
    condition: project.enable_knx
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    // KNX Secure's primitives come from the project's TLS backend - the system OpenSSL, or BoringSSL when
    // QUIC is enabled - which every binary links already. The headers stay out of the public KNX API: only
    // src/ and inc/ include them, so dependents need the libraries and not the include path.
    cpp.includePaths: [
        "../api",
        "../inc",
        "/usr/local/include",
    ].concat(project.tls_include_paths)
    install: true
    // A qbs "**.cpp" matches only the named directory, not the ones below it, so each subdirectory is listed.
    files: [
        "../api/kmx/aio/knx/**.hpp",
        "../api/kmx/aio/knx/detail/**.hpp",
        "../api/kmx/aio/knx/secure/**.hpp",
        "../inc/kmx/aio/knx/**.hpp",
        "../inc/kmx/aio/knx/detail/**.hpp",
        "../inc/kmx/aio/knx/secure/**.hpp",
        "../inc/kmx/aio/knx/secure/detail/**.hpp",
        "../src/kmx/aio/knx/**.cpp",
        "../src/kmx/aio/knx/secure/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx_features" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
        cpp.dynamicLibraries: project.tls_libraries
    }
}
