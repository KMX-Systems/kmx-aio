import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx_instrumentation" }

    name: "kmx-aio-core"
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
    ].concat(project.tls_include_paths)
    install: true
    files: [
        "../api/kmx/aio/allocator.hpp",
        "../api/kmx/aio/async_mutex.hpp",
        "../api/kmx/aio/basic_channel.hpp",
        "../api/kmx/aio/basic_types.hpp",
        "../api/kmx/aio/buffer.hpp",
        "../api/kmx/aio/buffer_pool.hpp",
        "../api/kmx/aio/channel.hpp",
        "../api/kmx/aio/detail/syscalls.hpp",
        "../api/kmx/aio/error_code.hpp",
        "../api/kmx/aio/executor_base.hpp",
        "../api/kmx/aio/file_descriptor.hpp",
        "../api/kmx/aio/ipv4.hpp",
        "../api/kmx/aio/ipv6.hpp",
        "../api/kmx/aio/mac.hpp",
        "../api/kmx/aio/scheduler.hpp",
        "../api/kmx/aio/stream_concepts.hpp",
        "../api/kmx/aio/task.hpp",
        "../api/kmx/aio/tls/basic_stream.hpp",
        "../api/kmx/aio/tls/detail/tls_syscalls.hpp",
        "../api/kmx/aio/tls/stream.hpp",
        "../src/kmx/aio/allocator.cpp",
        "../src/kmx/aio/async_mutex.cpp",
        "../src/kmx/aio/basic_channel.cpp",
        "../src/kmx/aio/basic_types.cpp",
        "../src/kmx/aio/detail/syscalls.cpp",
        "../src/kmx/aio/error_code.cpp",
        "../src/kmx/aio/file_descriptor.cpp",
        "../src/kmx/aio/net_parse.cpp",
        "../src/kmx/aio/scheduler.cpp",
        "../src/kmx/aio/task.cpp",
        "../src/kmx/aio/tls/basic_stream.cpp",
        "../src/kmx/aio/tls/detail/tls_syscalls.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx_instrumentation" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ].concat(project.tls_include_paths)
    }
}
