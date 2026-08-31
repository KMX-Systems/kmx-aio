import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx-aio-completion" }
    Depends { name: "kmx_instrumentation" }

    name: "kmx-aio-xdp"
    condition: project.enable_af_xdp
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
        if (project.enable_cuda)
            defs.push("KMX_AIO_FEATURE_CUDA=1");
        return defs;
    }
    cpp.includePaths: [
        "../api",
        "/usr/local/include",
    ]
    cpp.dynamicLibraries: [
        "xdp",
        "bpf",
    ]
    install: true
    files: [
        "../api/kmx/aio/completion/xdp/**.hpp",
        "../src/kmx/aio/completion/xdp/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx-aio-completion" }
        Depends { name: "kmx_instrumentation" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}
