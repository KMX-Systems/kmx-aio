import qbs
import qbs.File

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }

    name: "kmx-aio-opcua"
    condition: project.enable_opc_ua
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
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
        "../inc/kmx/aio/opc_ua/detail/**.hpp",
        "../src/kmx/aio/opc_ua/**.cpp",
        "../src/kmx/aio/opc_ua/detail/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx_instrumentation" }
        Depends { name: "kmx_features" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}
