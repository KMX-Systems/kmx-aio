import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx_instrumentation" }

    name: "kmx-aio-someip"
    condition: project.enable_someip
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.defines: {
        var defs = [];
        if (project.enable_openonload)
            defs.push("KMX_AIO_FEATURE_OPENONLOAD=1");
        if (project.enable_af_xdp)
            defs.push("KMX_AIO_FEATURE_AF_XDP=1");
        if (project.enable_spdk)
            defs.push("KMX_AIO_FEATURE_SPDK=1");
        if (project.enable_quic)
            defs.push("KMX_AIO_FEATURE_QUIC=1");
        if (project.enable_avb)
            defs.push("KMX_AIO_FEATURE_AVB=1");
        if (project.enable_opc_ua)
            defs.push("KMX_AIO_FEATURE_OPC_UA=1");
        if (project.enable_someip)
            defs.push("KMX_AIO_FEATURE_SOMEIP=1");
        if (project.someip_link_backend)
            defs.push("KMX_AIO_SOMEIP_LINK_BACKEND=1");
        if (project.enable_cuda)
            defs.push("KMX_AIO_FEATURE_CUDA=1");
        return defs;
    }
    cpp.includePaths: [
        "../api",
        "../inc",
        "/usr/local/include",
        project.someip_prefix ? project.someip_prefix + "/include" : "",
    ]
    cpp.libraryPaths: [
        project.someip_prefix ? project.someip_prefix + "/lib" : "",
    ]
    cpp.dynamicLibraries: {
        var libs = [];

        if (project.someip_link_backend)
        {
            libs.push("vsomeip3");
        }

        return libs;
    }
    install: true
    files: [
        "../api/kmx/aio/someip/**.hpp",
        "../inc/kmx/aio/someip/**.hpp",
        "../src/kmx/aio/someip/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx_instrumentation" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
        cpp.defines: {
            var defs = [];
            if (project.someip_link_backend)
                defs.push("KMX_AIO_SOMEIP_LINK_BACKEND=1");
            return defs;
        }
    }
}
