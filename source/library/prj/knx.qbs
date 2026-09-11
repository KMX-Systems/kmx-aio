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
        "../api/kmx/aio/knx/data_secure/**.hpp",
        "../api/kmx/aio/knx/detail/**.hpp",
        "../api/kmx/aio/knx/dib/**.hpp",
        "../api/kmx/aio/knx/discovery/**.hpp",
        "../api/kmx/aio/knx/dpt/**.hpp",
        "../api/kmx/aio/knx/keyring/**.hpp",
        "../api/kmx/aio/knx/routing/**.hpp",
        "../api/kmx/aio/knx/secure/**.hpp",
        "../inc/kmx/aio/knx/**.hpp",
        "../inc/kmx/aio/knx/data_secure/detail/**.hpp",
        "../inc/kmx/aio/knx/detail/**.hpp",
        "../inc/kmx/aio/knx/routing/detail/**.hpp",
        "../inc/kmx/aio/knx/secure/**.hpp",
        "../inc/kmx/aio/knx/secure/detail/**.hpp",
        "../src/kmx/aio/knx/**.cpp",
        "../src/kmx/aio/knx/data_secure/**.cpp",
        "../src/kmx/aio/knx/detail/**.cpp",
        "../src/kmx/aio/knx/dib/**.cpp",
        "../src/kmx/aio/knx/discovery/**.cpp",
        "../src/kmx/aio/knx/keyring/**.cpp",
        "../src/kmx/aio/knx/routing/**.cpp",
        "../src/kmx/aio/knx/routing/detail/**.cpp",
        "../src/kmx/aio/knx/secure/**.cpp",
        "../src/kmx/aio/knx/secure/detail/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx_features" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
        cpp.dynamicLibraries: project.tls_libraries
    }
}
