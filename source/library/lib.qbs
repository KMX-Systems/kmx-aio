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
        return defs;
    }
    cpp.includePaths: [
        "inc",
        "inc_dep",
        "/usr/local/include",
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
            libs.push("onload_ext");
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

        return libs;
    }
    install: true
    name: "kmx-aio-lib"
    files: [
        // Public headers
        "inc/kmx/**.hpp",

        // Core/runtime sources
        "src/kmx/aio/**.cpp",

        // Completion model sources
        "inc/kmx/aio/completion/**.hpp",
        "inc/kmx/aio/completion/tcp/**.hpp",
        "inc/kmx/aio/completion/udp/**.hpp",
        "inc/kmx/aio/completion/tls/**.hpp",
        "inc/kmx/aio/completion/spdk/**.hpp",
        "inc/kmx/aio/completion/xdp/**.hpp",
        "src/kmx/aio/completion/**.cpp",
        "src/kmx/aio/completion/tcp/**.cpp",
        "src/kmx/aio/completion/udp/**.cpp",
        "src/kmx/aio/completion/tls/**.cpp",
        "src/kmx/aio/completion/spdk/**.cpp",
        "src/kmx/aio/completion/xdp/**.cpp",

        // Readiness model sources
        "inc/kmx/aio/readiness/**.hpp",
        "inc/kmx/aio/readiness/descriptor/**.hpp",
        "inc/kmx/aio/readiness/tcp/**.hpp",
        "inc/kmx/aio/readiness/udp/**.hpp",
        "inc/kmx/aio/readiness/tls/**.hpp",
        "src/kmx/aio/readiness/**.cpp",
        "src/kmx/aio/readiness/descriptor/**.cpp",
        "src/kmx/aio/readiness/tcp/**.cpp",
        "src/kmx/aio/readiness/udp/**.cpp",
        "src/kmx/aio/readiness/tls/**.cpp",
    ]
}
