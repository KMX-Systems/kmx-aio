import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }

    name: "kmx-aio-readiness"
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
        "/usr/local/include",
    ]
    cpp.dynamicLibraries: [
        "pthread",
    ]
    install: true
    files: [
        "../api/kmx/aio/readiness/**.hpp",
        "../api/kmx/aio/readiness/descriptor/**.hpp",
        "../api/kmx/aio/readiness/tcp/**.hpp",
        "../api/kmx/aio/readiness/udp/**.hpp",
        "../api/kmx/aio/readiness/tls/**.hpp",
        "../api/kmx/aio/readiness/v4l2/**.hpp",
        "../src/kmx/aio/readiness/executor.cpp",
        "../src/kmx/aio/readiness/descriptor/**.cpp",
        "../src/kmx/aio/readiness/tcp/**.cpp",
        "../src/kmx/aio/readiness/udp/**.cpp",
        "../src/kmx/aio/readiness/tls/**.cpp",
        "../src/kmx/aio/readiness/v4l2/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}
