import qbs
import qbs.File

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx-aio-readiness"; condition: project.enable_readiness }
    Depends { name: "kmx-aio-completion"; condition: project.enable_completion }
    Depends { name: "kmx-aio-http2"; condition: project.enable_http2 }
    Depends { name: "kmx-aio-http3"; condition: project.enable_http3 && project.enable_quic }
    Depends { name: "kmx-aio-gpu"; condition: project.enable_cuda }
    Depends { name: "kmx-aio-opcua"; condition: project.enable_opc_ua }
    Depends { name: "kmx-aio-modbus"; condition: project.enable_modbus }
    Depends { name: "kmx-aio-knx"; condition: project.enable_knx }
    Depends { name: "kmx-aio-someip"; condition: project.enable_someip }
    Depends { name: "kmx-aio-quic"; condition: project.enable_quic }
    Depends { name: "kmx-aio-xdp"; condition: project.enable_af_xdp }
    Depends { name: "kmx-aio-spdk"; condition: project.enable_spdk }
    Depends { name: "kmx-aio-avb"; condition: project.enable_avb }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "api",
        "inc",
        "inc_dep",
        "/usr/local/include",
        project.enable_spdk && project.spdk_prefix ? project.spdk_prefix + "/include" : "",
        project.enable_spdk && project.spdk_prefix ? project.spdk_prefix + "/include/dpdk" : "",
        project.enable_opc_ua && project.opc_ua_prefix ? project.opc_ua_prefix + "/include" : "",
        project.enable_someip && project.someip_prefix ? project.someip_prefix + "/include" : "",
    ].concat(project.quic_include_paths).concat(project.tls_include_paths)
    cpp.libraryPaths: [
        "/usr/local/lib",
        project.enable_spdk && project.spdk_prefix ? project.spdk_prefix + "/lib" : "",
        project.enable_spdk && project.spdk_prefix ? project.spdk_prefix + "/lib64" : "",
        project.enable_opc_ua && project.opc_ua_prefix ? project.opc_ua_prefix + "/lib" : "",
        project.enable_someip && project.someip_prefix ? project.someip_prefix + "/lib" : "",
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

            // ISA-L only exists when SPDK found nasm 2.14+ at configure time; naming it against a
            // prefix that has none turns a working SPDK into "cannot find -lisal". Follow the
            // prefix, exactly as kmx-aio-spdk does.
            var spdkPrefix = project.spdk_prefix;
            if (spdkPrefix)
            {
                if (File.exists(spdkPrefix + "/lib/libisal.so") || File.exists(spdkPrefix + "/lib/libisal.a") ||
                    File.exists(spdkPrefix + "/lib64/libisal.so") || File.exists(spdkPrefix + "/lib64/libisal.a"))
                    libs.push("isal");

                if (File.exists(spdkPrefix + "/lib/libisal_crypto.so") || File.exists(spdkPrefix + "/lib/libisal_crypto.a") ||
                    File.exists(spdkPrefix + "/lib64/libisal_crypto.so") || File.exists(spdkPrefix + "/lib64/libisal_crypto.a"))
                    libs.push("isal_crypto");
            }
        }

        if (project.enable_quic)
            libs = libs.concat(project.quic_libraries);

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

        if (project.enable_someip && project.someip_link_backend)
        {
            libs.push("vsomeip3");
        }

        if (project.enable_cuda)
        {
            libs.push("cudart");
        }

        return libs;
    }
    install: true
    name: "kmx-aio-lib"

    // The public headers, installed as a tree. installSourceBase strips the api/ prefix, so
    // api/kmx/aio/task.hpp lands at include/kmx/aio/task.hpp and an installed tree can be compiled
    // against with one -I<install-root>/include. They are installed from here and not from the
    // sub-libraries: each of those lists its own share of api/ by name, and this is the one product
    // that carries the whole directory, so this is the only place the tree is described once.
    //
    // HTTP/2, GPU, OPC UA and AVB are provided by dedicated sub-libraries; their headers live under
    // the same api/ root and are covered by the wildcard below.
    Group {
        // "api/**/*.hpp", not "api/kmx/**.hpp": qbs reads ** as a whole path component, so the latter
        // matches api/kmx/*.hpp and nothing below it. That is what the two patterns this replaced were
        // working around, and between them they still reached only two levels of the tree.
        name: "public headers"
        files: ["api/**/*.hpp"]
        qbs.install: true
        qbs.installDir: "include"
        qbs.installSourceBase: "api"
    }

    // The one installed header that is not under api/: source/source.qbs writes it into the build
    // directory during "qbs resolve", from the feature flags this build was configured with. It is
    // what tells code compiled against the installed tree which of the optional APIs below are really
    // there, so it is installed beside them rather than left behind in the build directory.
    Group {
        name: "generated feature configuration"
        files: [project.generated_include_dir + "/kmx/aio/config.hpp"]
        qbs.install: true
        qbs.installDir: "include/kmx/aio"
    }

    // Deliberately not installed: these are the library's own internals, and an installed tree that
    // carried them would invite code outside the library to include them.
    Group {
        name: "private headers"
        files: ["inc/**/*.hpp"]
    }
    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx-aio-readiness"; condition: project.enable_readiness }
        Depends { name: "kmx-aio-completion"; condition: project.enable_completion }
        Depends { name: "kmx-aio-http2"; condition: project.enable_http2 }
        Depends { name: "kmx-aio-http3"; condition: project.enable_http3 && project.enable_quic }
        Depends { name: "kmx-aio-gpu"; condition: project.enable_cuda }
        Depends { name: "kmx-aio-opcua"; condition: project.enable_opc_ua }
        Depends { name: "kmx-aio-someip"; condition: project.enable_someip }
        Depends { name: "kmx-aio-quic"; condition: project.enable_quic }
        Depends { name: "kmx-aio-xdp"; condition: project.enable_af_xdp }
        Depends { name: "kmx-aio-spdk"; condition: project.enable_spdk }
        Depends { name: "kmx-aio-avb"; condition: project.enable_avb }
        Depends { name: "kmx_instrumentation" }
        Depends { name: "kmx_features" }
        cpp.includePaths: [ product.sourceDirectory + "/api" ].concat(project.tls_include_paths)
        cpp.libraryPaths: [
            project.enable_spdk && project.spdk_prefix ? project.spdk_prefix + "/lib" : "",
            project.enable_spdk && project.spdk_prefix ? project.spdk_prefix + "/lib64" : "",
            project.enable_opc_ua && project.opc_ua_prefix ? project.opc_ua_prefix + "/lib" : "",
            project.enable_someip && project.someip_prefix ? project.someip_prefix + "/lib" : "",
        ]
    }
}
