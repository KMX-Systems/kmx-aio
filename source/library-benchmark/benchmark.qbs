import qbs

CppApplication {
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx-aio-readiness"; condition: project.enable_readiness }
    Depends { name: "kmx-aio-completion"; condition: project.enable_completion }
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
    Depends { name: "kmx_instrumentation" }

    name: "kmx-aio-benchmark"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false

    // Measurements taken from a build with assertions and debug checks in are measurements of the
    // checks. The benchmark is therefore always optimized, whichever configuration it is built in.
    cpp.optimization: "fast"
    cpp.debugInformation: false

    // The same gate set the test binary uses, so a case file can be written against exactly the
    // feature macros the rest of the tree is written against. A feature that is off leaves its cases
    // compiled out, and the pairing they belong to reports the missing side rather than disappearing.
    cpp.defines: {
        var defs = ["NDEBUG"];
        if (project.enable_readiness)
            defs.push("KMX_AIO_FEATURE_READINESS=1");
        if (project.enable_completion)
            defs.push("KMX_AIO_FEATURE_COMPLETION=1");
        if (project.enable_openonload)
            defs.push("KMX_AIO_FEATURE_OPENONLOAD=1");
        if (project.enable_af_xdp)
            defs.push("KMX_AIO_FEATURE_AF_XDP=1");
        if (project.enable_spdk)
            defs.push("KMX_AIO_FEATURE_SPDK=1");
        if (project.enable_quic)
            defs.push("KMX_AIO_FEATURE_QUIC=1");
        if (project.enable_http2)
            defs.push("KMX_AIO_FEATURE_HTTP2=1");
        if (project.enable_http3)
            defs.push("KMX_AIO_FEATURE_HTTP3=1");
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
        return defs;
    }

    // ../library-test/inc is here for the test tree's self-contained helpers - the certificate pair a
    // TLS scenario needs, and the machine probes a scenario skips itself on. Copying them into the
    // benchmark would leave two versions to keep in step; the benchmark takes no dependency on the
    // test binary itself.
    cpp.includePaths: [
        "inc",
        "../library/api",
        "../library/inc",
        "../library-test/inc",
        project.enable_opc_ua && project.opc_ua_prefix ? project.opc_ua_prefix + "/include" : "",
        project.enable_someip && project.someip_prefix ? project.someip_prefix + "/include" : ""
    ].concat(project.quic_include_paths).concat(project.tls_include_paths)

    cpp.dynamicLibraries: {
        var libs = ["pthread"];
        if (project.enable_completion)
            libs.push("uring");
        return libs.concat(project.tls_libraries);
    }

    files: [
        "inc/kmx/aio/benchmark/**.hpp",
        "src/**/*.cpp",
    ]
}
