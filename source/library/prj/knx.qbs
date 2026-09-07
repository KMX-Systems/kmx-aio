import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx_instrumentation" }

    name: "kmx-aio-knx"
    condition: project.enable_knx
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.defines: {
        var defs = [];
        defs.push("KMX_AIO_FEATURE_KNX=1");
        if (project.enable_knx_secure)
            defs.push("KMX_AIO_FEATURE_KNX_SECURE=1");
        if (project.enable_knx_keyring)
            defs.push("KMX_AIO_FEATURE_KNX_KEYRING=1");
        return defs;
    }
    cpp.includePaths: [
        "../api",
        "../inc",
        "/usr/local/include",
    ]
    install: true
    files: [
        "../api/kmx/aio/knx/**.hpp",
        "../inc/kmx/aio/knx/**.hpp",
        "../src/kmx/aio/knx/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}
