import qbs

StaticLibrary {
    Depends { name: "cpp" }
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
        return defs;
    }
    cpp.includePaths: [
        "api",
        "inc",
        "inc_dep",
        "/usr/local/include",
        "../../build/lsquic/include",
    ]
    cpp.libraryPaths: [
        "/usr/local/lib",
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
            libs.push("rte_argparse");
            libs.push("rte_mempool_ring");
            libs.push("rte_mempool");
            libs.push("rte_ring");
            libs.push("rte_bus_pci");
            libs.push("rte_pci");
            libs.push("rte_power");
            libs.push("rte_timer");
            libs.push("rte_power_acpi");
            libs.push("rte_power_amd_pstate");
            libs.push("rte_power_cppc");
            libs.push("rte_power_intel_pstate");
            libs.push("rte_power_intel_uncore");
            libs.push("rte_power_kvm_vm");
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
            libs.push("isal");
            libs.push("isal_crypto");
        }

        if (project.enable_quic)
        {
            libs.push("/home/io/Development/kmx-aio/build/lsquic/build/src/liblsquic/liblsquic.a");
            libs.push("/home/io/Development/3rd/boringssl/build/libssl.a");
            libs.push("/home/io/Development/3rd/boringssl/build/libcrypto.a");
            libs.push("z");
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

        // Core/runtime sources
        "src/kmx/aio/**.cpp",

        // Completion model sources
        "api/kmx/aio/completion/**.hpp",
        "api/kmx/aio/completion/tcp/**.hpp",
        "api/kmx/aio/completion/udp/**.hpp",
        "api/kmx/aio/completion/tls/**.hpp",
        "api/kmx/aio/completion/spdk/**.hpp",
        "api/kmx/aio/completion/xdp/**.hpp",
        "src/kmx/aio/completion/**.cpp",
        "src/kmx/aio/completion/tcp/**.cpp",
        "src/kmx/aio/completion/udp/**.cpp",
        "src/kmx/aio/completion/tls/**.cpp",
        "src/kmx/aio/completion/spdk/**.cpp",
        "src/kmx/aio/completion/xdp/**.cpp",
        "src/kmx/aio/completion/quic/**.cpp",

        // Readiness model sources
        "api/kmx/aio/readiness/**.hpp",
        "api/kmx/aio/readiness/descriptor/**.hpp",
        "api/kmx/aio/readiness/tcp/**.hpp",
        "api/kmx/aio/readiness/udp/**.hpp",
        "api/kmx/aio/readiness/tls/**.hpp",
        "src/kmx/aio/readiness/**.cpp",
        "src/kmx/aio/readiness/descriptor/**.cpp",
        "src/kmx/aio/readiness/tcp/**.cpp",
        "src/kmx/aio/readiness/udp/**.cpp",
        "src/kmx/aio/readiness/tls/**.cpp",
        "src/kmx/aio/readiness/quic/**.cpp",
        "src/kmx/aio/quic/**.cpp",
    ]
    Export {
        Depends { name: "cpp" }
        cpp.includePaths: [
            product.sourceDirectory + "/api",
        ]
    }
}
