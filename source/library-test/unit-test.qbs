import qbs

CppApplication {
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx-aio-readiness"; condition: project.enable_readiness }
    Depends { name: "kmx-aio-completion" }
    Depends { name: "kmx-aio-http2"; condition: project.enable_http2 }
    Depends { name: "kmx-aio-http3"; condition: project.enable_http3 && project.enable_quic }
    Depends { name: "kmx-aio-gpu"; condition: project.enable_cuda }
    Depends { name: "kmx-aio-opcua"; condition: project.enable_opc_ua }
    Depends { name: "kmx-aio-modbus"; condition: project.enable_modbus }
    Depends { name: "kmx-aio-someip"; condition: project.enable_someip }
    Depends { name: "kmx-aio-quic"; condition: project.enable_quic }
    Depends { name: "kmx-aio-xdp"; condition: project.enable_af_xdp }
    Depends { name: "kmx-aio-spdk"; condition: project.enable_spdk }
    Depends { name: "kmx-aio-avb"; condition: project.enable_avb }

    name: "kmx-aio-test"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
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
        if (project.enable_someip)
            defs.push("KMX_AIO_FEATURE_SOMEIP=1");
        if (project.enable_cuda)
            defs.push("KMX_AIO_FEATURE_CUDA=1");
        if (project.enable_asan)
            defs.push("KMX_AIO_SANITIZER_ASAN=1");
        if (project.enable_tsan)
            defs.push("KMX_AIO_SANITIZER_TSAN=1");
        return defs;
    }
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
    cpp.includePaths: [
        "inc",
        "inc_dep",
        "../library/api",
        "../library/inc",
        "../sample/completion/avb/common/inc",
        "../sample/completion/v4l2/capture/inc",
        "../sample/readiness/v4l2/capture/inc",
        project.enable_opc_ua && project.opc_ua_prefix ? project.opc_ua_prefix + "/include" : "",
        project.enable_someip && project.someip_prefix ? project.someip_prefix + "/include" : ""
    ]
    cpp.dynamicLibraries: [
        "crypto",
        "ssl",
    ]
    cpp.staticLibraries: [
        "Catch2Main",
        "Catch2"
    ]
    files: [
        "inc/kmx/aio/**.hpp",
        "src/**/*.cpp",
    ]
    excludeFiles: {
        var files = [];

        if (!project.enable_modbus)
            files.push("src/kmx/aio/modbus/**.cpp");

        if (!project.enable_opc_ua)
            files.push("src/kmx/aio/opc_ua/**.cpp");

        if (!project.enable_someip)
            files.push("src/kmx/aio/someip/**.cpp");

        if (!project.enable_cuda)
        {
            files.push("src/kmx/aio/gpu/**.cpp");
            files.push("src/kmx/aio/integration/gpu_image_processing_smoke_test.cpp");
        }

        if (!project.enable_spdk)
        {
            files.push("src/kmx/aio/completion/spdk/**.cpp");
            files.push("src/kmx/aio/integration/pillar_1_integration_test.cpp");
        }

        if (!project.enable_af_xdp)
            files.push("src/kmx/aio/completion/xdp/**.cpp");

        if (!project.enable_avb)
            files.push("src/kmx/aio/avb/**/*.cpp");

        if (!project.enable_http2)
            files.push("src/kmx/aio/http2/**.cpp");

        if (!project.enable_http3)
            files.push("src/kmx/aio/http3/**.cpp");

        if (!project.enable_http3)
            files.push("src/kmx/aio/integration/quic_http3_smoke_test.cpp");

        if (!project.enable_readiness)
        {
            files.push("src/kmx/aio/integration/readiness_core_pinning_test.cpp");
            files.push("src/kmx/aio/integration/pillar_2_integration_test.cpp");
            files.push("src/kmx/aio/integration/quic_readiness_echo_smoke_test.cpp");
            files.push("src/kmx/aio/integration/quic_http3_smoke_test.cpp");
            files.push("src/kmx/aio/v4l2/readiness_*.cpp");
        }

        if (!project.enable_quic)
        {
            files.push("src/kmx/aio/integration/quic_readiness_echo_smoke_test.cpp");
        }

        return files;
    }
}
