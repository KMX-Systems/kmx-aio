import qbs
import qbs.File

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx-aio-completion" }
    Depends { name: "kmx_instrumentation" }

    name: "kmx-aio-spdk"
    condition: project.enable_spdk
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

        // SPDK only builds ISA-L when nasm 2.14+ is present at configure time; without it the
        // prefix holds no libisal at all and its pkg-config files reference none, so linking these
        // unconditionally fails a build against an otherwise working SPDK. Follow the prefix.
        var prefix = project.spdk_prefix;
        if (prefix)
        {
            var hasIsal = File.exists(prefix + "/lib/libisal.so") || File.exists(prefix + "/lib/libisal.a") ||
                          File.exists(prefix + "/lib64/libisal.so") || File.exists(prefix + "/lib64/libisal.a");
            if (hasIsal)
                libs.push("isal");

            var hasIsalCrypto = File.exists(prefix + "/lib/libisal_crypto.so") || File.exists(prefix + "/lib/libisal_crypto.a") ||
                                File.exists(prefix + "/lib64/libisal_crypto.so") || File.exists(prefix + "/lib64/libisal_crypto.a");
            if (hasIsalCrypto)
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
        Depends { name: "kmx_instrumentation" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
        cpp.libraryPaths: [
            project.spdk_prefix ? project.spdk_prefix + "/lib" : "",
            project.spdk_prefix ? project.spdk_prefix + "/lib64" : "",
        ]

        // libspdk_util.so and libspdk_accel.so are shipped with their ISA-L symbols undefined, and
        // no program calls into them directly - they arrive through libspdk_bdev's DT_NEEDED. Under
        // --as-needed, which the GCC that Ubuntu ships turns on by default, the linker therefore
        // drops them where they sit on the command line, reads past -lisal with nothing left to
        // resolve, and only pulls them back in once the archive is behind it. The result is a wall
        // of "undefined reference to `isal_inflate'" against a prefix where ISA-L is present and
        // correct. Keep every SPDK library the moment it is named, and ISA-L resolves them in place.
        cpp.linkerFlags: [ "--no-as-needed" ]
    }
}
