import qbs

CppApplication {
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx-aio-readiness"; condition: project.enable_readiness }
    Depends { name: "kmx-aio-completion"; condition: project.enable_completion }
    Depends { name: "kmx_instrumentation" }

    name: "kmx-aio-benchmark"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false

    // Measurements taken from a build with assertions and debug checks in are measurements of the
    // checks. The benchmark is therefore always optimized, whichever configuration it is built in.
    cpp.optimization: "fast"
    cpp.debugInformation: false
    cpp.defines: {
        var defs = ["NDEBUG"];
        if (project.enable_readiness)
            defs.push("KMX_AIO_FEATURE_READINESS=1");
        if (project.enable_completion)
            defs.push("KMX_AIO_FEATURE_COMPLETION=1");
        return defs;
    }
    cpp.includePaths: [
        "inc",
        "../library/api",
        "../library/inc",
    ]
    cpp.dynamicLibraries: {
        var libs = ["pthread"];
        if (project.enable_completion)
            libs.push("uring");
        return libs;
    }
    files: [
        "inc/kmx/aio/benchmark/**.hpp",
        "src/**/*.cpp",
    ]
}
