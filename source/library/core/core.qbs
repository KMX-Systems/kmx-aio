import qbs

StaticLibrary {
    Depends { name: "cpp" }

    name: "kmx-aio-core"
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
    install: true
    files: [
        "../api/kmx/aio/allocator.hpp",
        "../api/kmx/aio/basic_types.hpp",
        "../api/kmx/aio/buffer.hpp",
        "../api/kmx/aio/buffer_pool.hpp",
        "../api/kmx/aio/channel.hpp",
        "../api/kmx/aio/error_code.hpp",
        "../api/kmx/aio/executor_base.hpp",
        "../api/kmx/aio/file_descriptor.hpp",
        "../api/kmx/aio/scheduler.hpp",
        "../api/kmx/aio/stream_concepts.hpp",
        "../api/kmx/aio/task.hpp",
        "../src/kmx/aio/allocator.cpp",
        "../src/kmx/aio/basic_types.cpp",
        "../src/kmx/aio/error_code.cpp",
        "../src/kmx/aio/file_descriptor.cpp",
        "../src/kmx/aio/scheduler.cpp",
        "../src/kmx/aio/task.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}
