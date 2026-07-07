import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx-aio-completion" }

    name: "kmx-aio-spdk"
    condition: project.enable_spdk
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
        project.spdk_prefix ? project.spdk_prefix + "/include" : "",
        project.spdk_prefix ? project.spdk_prefix + "/include/dpdk" : "",
    ]
    cpp.libraryPaths: [
        project.spdk_prefix ? project.spdk_prefix + "/lib" : "",
        project.spdk_prefix ? project.spdk_prefix + "/lib64" : "",
    ]
    cpp.dynamicLibraries: {
        var libs = [
            "spdk_env_dpdk",
            "spdk_bdev",
            "spdk_nvme",
            "spdk_accel",
            "spdk_init",
            "spdk_thread",
            "spdk_util",
            "spdk_log",
            "rte_eal",
            "rte_kvargs",
            "rte_log",
            "rte_telemetry",
            "rte_mempool_ring",
            "rte_mempool",
            "rte_ring",
            "rte_bus_pci",
            "rte_pci",
            "rte_power",
            "rte_timer",
            "rte_vhost",
            "rte_ethdev",
            "rte_meter",
            "rte_cryptodev",
            "rte_dmadev",
            "rte_hash",
            "rte_net",
            "rte_mbuf",
            "rte_rcu",
            "ssl",
            "crypto",
        ];

        if (project.spdk_enable_crypto)
        {
            libs.push("isal");
            libs.push("isal_crypto");
        }

        return libs;
    }
    install: true
    files: [
        "../api/kmx/aio/completion/spdk/**.hpp",
        "../src/kmx/aio/completion/spdk/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx-aio-completion" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
        cpp.libraryPaths: [
            project.spdk_prefix ? project.spdk_prefix + "/lib" : "",
            project.spdk_prefix ? project.spdk_prefix + "/lib64" : "",
        ]
    }
}
