import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx-aio-readiness" }
    Depends { name: "kmx-aio-completion" }
    Depends { name: "kmx-aio-http2" }
    Depends { name: "kmx-aio-gpu"; condition: project.enable_cuda }
    Depends { name: "kmx-aio-opcua"; condition: project.enable_opc_ua }
    Depends { name: "kmx-aio-quic"; condition: project.enable_quic }
    Depends { name: "kmx-aio-xdp"; condition: project.enable_af_xdp }
    Depends { name: "kmx-aio-spdk"; condition: project.enable_spdk }
    Depends { name: "kmx-aio-avb"; condition: project.enable_avb }
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
        "api",
        "inc",
        "inc_dep",
        "/usr/local/include",
        project.enable_spdk && project.spdk_prefix ? project.spdk_prefix + "/include" : "",
        project.enable_spdk && project.spdk_prefix ? project.spdk_prefix + "/include/dpdk" : "",
        "../../build/lsquic/include",
        project.enable_opc_ua && project.opc_ua_prefix ? project.opc_ua_prefix + "/include" : "",
    ]
    cpp.libraryPaths: [
        "/usr/local/lib",
        project.enable_spdk && project.spdk_prefix ? project.spdk_prefix + "/lib" : "",
        project.enable_spdk && project.spdk_prefix ? project.spdk_prefix + "/lib64" : "",
        project.enable_opc_ua && project.opc_ua_prefix ? project.opc_ua_prefix + "/lib" : "",
    ]
    cpp.dynamicLibraries: {
        var libs = ["uring", "pthread"];
        if (project.enable_af_xdp)
        {
            libs.push("xdp");
            libs.push("bpf");
        }

        if (project.enable_openonload)
        {
            // OpenOnload calls are header-gated in the implementation,
            // so do not force a hard link dependency on hosts without libonload_ext.
        }

        if (project.enable_spdk)
        {
            libs.push("spdk_env_dpdk");
            libs.push("spdk_bdev");
            libs.push("spdk_nvme");
            libs.push("spdk_accel");
            libs.push("spdk_init");
            libs.push("spdk_thread");
            libs.push("spdk_util");
            libs.push("spdk_log");

            // SPDK shared libraries in this environment do not carry all
            // transitive dependencies, so we link required backend deps
            // explicitly (DPDK + crypto + ISA-L).
            libs.push("rte_eal");
            libs.push("rte_kvargs");
            libs.push("rte_log");
            libs.push("rte_telemetry");
            libs.push("rte_mempool_ring");
            libs.push("rte_mempool");
            libs.push("rte_ring");
            libs.push("rte_bus_pci");
            libs.push("rte_pci");
            libs.push("rte_power");
            libs.push("rte_timer");
            libs.push("rte_vhost");
            libs.push("rte_ethdev");
            libs.push("rte_meter");
            libs.push("rte_cryptodev");
            libs.push("rte_dmadev");
            libs.push("rte_hash");
            libs.push("rte_net");
            libs.push("rte_mbuf");
            libs.push("rte_rcu");

            libs.push("ssl");
            libs.push("crypto");

            if (project.spdk_enable_crypto)
            {
                libs.push("isal");
                libs.push("isal_crypto");
            }
        }

        if (project.enable_quic)
        {
            libs.push(product.sourceDirectory + "/../../build/lsquic/build/src/liblsquic/liblsquic.a");
            libs.push(product.sourceDirectory + "/../../build/boringssl/build/libssl.a");
            libs.push(product.sourceDirectory + "/../../build/boringssl/build/libcrypto.a");
            libs.push("z");
        }

        if (project.enable_opc_ua)
        {
            if (project.opc_ua_vendored && project.opc_ua_prefix)
                libs.push(project.opc_ua_prefix + "/lib/libopen62541.a");
            else
                libs.push("open62541");

            // Encryption-enabled open62541 builds may require explicit crypto deps.
            libs.push("ssl");
            libs.push("crypto");
        }

        if (project.enable_cuda)
        {
            libs.push("cudart");
        }

        return libs;
    }
    install: true
    name: "kmx-aio-lib"
    files: [
        // Public headers
        "api/kmx/**.hpp",
        "api/kmx/aio/**.hpp",

        // Private headers
        "inc/kmx/aio/**.hpp",

        // HTTP/2, GPU and OPC UA are provided by dedicated sub-libraries

        // AVB is provided by dedicated sub-library
    ]
    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx-aio-readiness" }
        Depends { name: "kmx-aio-completion" }
        Depends { name: "kmx-aio-http2" }
        Depends { name: "kmx-aio-gpu"; condition: project.enable_cuda }
        Depends { name: "kmx-aio-opcua"; condition: project.enable_opc_ua }
        Depends { name: "kmx-aio-quic"; condition: project.enable_quic }
        Depends { name: "kmx-aio-xdp"; condition: project.enable_af_xdp }
        Depends { name: "kmx-aio-spdk"; condition: project.enable_spdk }
        Depends { name: "kmx-aio-avb"; condition: project.enable_avb }
        cpp.includePaths: [ product.sourceDirectory + "/api" ]
        cpp.libraryPaths: [
            project.enable_spdk && project.spdk_prefix ? project.spdk_prefix + "/lib" : "",
            project.enable_spdk && project.spdk_prefix ? project.spdk_prefix + "/lib64" : "",
            project.enable_opc_ua && project.opc_ua_prefix ? project.opc_ua_prefix + "/lib" : "",
        ]
    }
}
