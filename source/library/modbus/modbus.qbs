import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx-aio-readiness" }
    Depends { name: "kmx_instrumentation" }

    name: "kmx-aio-modbus"
    condition: project.enable_modbus
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
        if (project.enable_modbus)
            defs.push("KMX_AIO_FEATURE_MODBUS=1");
        if (project.enable_cuda)
            defs.push("KMX_AIO_FEATURE_CUDA=1");
        return defs;
    }
    cpp.includePaths: [
        "../api",
        "../inc",
        "/usr/local/include",
    ].concat(project.tls_include_paths)
    install: true
    files: [
        "../api/kmx/aio/modbus/**.hpp",
        "../src/kmx/aio/modbus/error.cpp",
        "../src/kmx/aio/modbus/frame.cpp",
        "../src/kmx/aio/modbus/client.cpp",
        "../src/kmx/aio/modbus/server.cpp",
        "../src/kmx/aio/modbus/tls_client.cpp",
        "../src/kmx/aio/modbus/tls_server.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx-aio-readiness" }
        Depends { name: "kmx_instrumentation" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ].concat(project.tls_include_paths)
    }
}
