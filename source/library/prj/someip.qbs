import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }

    name: "kmx-aio-someip"
    condition: project.enable_someip
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "../api",
        "../inc",
        "/usr/local/include",
        project.someip_prefix ? project.someip_prefix + "/include" : "",
    ]
    cpp.libraryPaths: [
        project.someip_prefix ? project.someip_prefix + "/lib" : "",
    ]
    cpp.dynamicLibraries: {
        var libs = [];

        if (project.someip_link_backend)
        {
            libs.push("vsomeip3");
        }

        return libs;
    }
    install: true
    files: [
        "../api/kmx/aio/someip/**.hpp",
        "../inc/kmx/aio/someip/**.hpp",
        "../src/kmx/aio/someip/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx_instrumentation" }
        Depends { name: "kmx_features" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ]
    }
}
