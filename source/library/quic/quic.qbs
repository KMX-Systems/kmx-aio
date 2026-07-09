import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }

    name: "kmx-aio-quic"
    condition: project.enable_quic
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.driverFlags: {
        var flags = [];
        if (project.enable_asan)
        {
            flags.push("-fsanitize=address");
            flags.push("-fno-omit-frame-pointer");
        }
        if (project.enable_tsan)
        {
            flags.push("-fsanitize=thread");
            flags.push("-fno-omit-frame-pointer");
        }
        return flags;
    }
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
        if (project.enable_asan)
            defs.push("KMX_AIO_SANITIZER_ASAN=1");
        if (project.enable_tsan)
            defs.push("KMX_AIO_SANITIZER_TSAN=1");
        return defs;
    }
    cpp.includePaths: [
        "../api",
        "../inc",
        "/usr/local/include",
        "../../../build/lsquic/include",
    ]
    cpp.dynamicLibraries: [
        product.sourceDirectory + "/../../../build/lsquic/build/src/liblsquic/liblsquic.a",
        product.sourceDirectory + "/../../../build/boringssl/build/libssl.a",
        product.sourceDirectory + "/../../../build/boringssl/build/libcrypto.a",
        "z",
    ]
    install: true
    files: {
        var entries = [
            "../api/kmx/aio/quic/**.hpp",
            "../api/kmx/aio/completion/quic/**.hpp",
            "../inc/kmx/aio/quic/**.hpp",
        ];

        if (project.enable_readiness)
            entries.push("../api/kmx/aio/readiness/quic/**.hpp");

        return entries;
    }

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}
