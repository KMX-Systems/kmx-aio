import qbs
import qbs.File

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx_instrumentation" }

    name: "kmx-aio-opcua"
    condition: project.enable_opc_ua
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
        "../inc",
        "/usr/local/include",
        project.opc_ua_prefix ? project.opc_ua_prefix + "/include" : "",
    ]
    cpp.libraryPaths: [
        project.opc_ua_prefix ? project.opc_ua_prefix + "/lib" : "",
    ]
    cpp.dynamicLibraries: {
        var libs = [];

        if (project.opc_ua_vendored && project.opc_ua_prefix)
            libs.push(project.opc_ua_prefix + "/lib/libopen62541.a");
        else
            libs.push("open62541");

        libs.push("ssl");
        libs.push("crypto");
        return libs;
    }
    install: true
    files: [
        "../api/kmx/aio/opc_ua/**.hpp",
        "../inc/kmx/aio/opc_ua/**.hpp",
        "../src/kmx/aio/opc_ua/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx_instrumentation" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}
