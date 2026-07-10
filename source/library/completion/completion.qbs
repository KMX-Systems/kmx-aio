import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }

    name: "kmx-aio-completion"
    condition: project.enable_completion
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
        project.enable_quic ? "../../../build/lsquic/include" : "",
    ]
    cpp.dynamicLibraries: [
        "uring",
        "pthread",
    ]
    install: true
    files: {
        var entries = [
            "../api/kmx/aio/completion/**.hpp",
            "../api/kmx/aio/completion/tcp/**.hpp",
            "../api/kmx/aio/completion/udp/**.hpp",
            "../api/kmx/aio/completion/tls/**.hpp",
            "../api/kmx/aio/completion/v4l2/**.hpp",
            "../src/kmx/aio/completion/executor.cpp",
            "../src/kmx/aio/completion/timer.cpp",
            "../src/kmx/aio/completion/avb/**.cpp",
            "../src/kmx/aio/completion/tcp/**.cpp",
            "../src/kmx/aio/completion/udp/**.cpp",
            "../src/kmx/aio/completion/tls/**.cpp",
            "../src/kmx/aio/completion/v4l2/**.cpp",
        ];

        if (project.enable_quic)
        {
            entries.push("../src/kmx/aio/completion/quic/**.cpp");
            entries.push("../src/kmx/aio/quic/base_engine.cpp");
        }

        return entries;
    }

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}
