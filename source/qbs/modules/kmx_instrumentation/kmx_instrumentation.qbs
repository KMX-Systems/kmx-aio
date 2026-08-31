import qbs

/*
    Sanitizer and coverage instrumentation for the products of this project.

    Instrumentation is only meaningful when a whole binary agrees on it. A static library compiled
    with -fsanitize=address needs the executable linking it to pull in the ASan runtime, or the link
    fails on undefined __asan_* symbols; a coverage build that instruments the libraries but not the
    samples leaves the sample code out of the report entirely rather than showing it as uncovered.
    Repeating the flags in every product is what makes that agreement easy to break - and it had:
    kmx-aio-http3 and all of the samples carried no sanitizer flags at all, so enabling ASan broke
    their link.

    So the flags are decided once, here, and a product joins in by depending on this module. Each
    library also re-exports the dependency, which is what carries the instrumentation out to the
    samples and to the unit-test binary without every one of those files having to name it.

    The switches stay on the project, so a build sets them the way this project sets everything else -
    on the qbs command line, or in Qt Creator's project settings:

        qbs build -f source.qbs config:debug project.enable_asan:true project.enable_ubsan:true
        qbs build -f source.qbs config:debug project.enable_coverage:true

    script/run-sanitizer-tests.sh and script/run-coverage.sh drive those builds, and set the runtime
    environment the resulting binaries need.
*/
Module {
    Depends { name: "cpp" }

    property bool asan: project.enable_asan
    property bool ubsan: project.enable_ubsan
    property bool tsan: project.enable_tsan
    property bool coverage: project.enable_coverage
    property bool faultInjection: project.enable_fault_injection

    readonly property bool anySanitizer: asan || ubsan || tsan

    readonly property stringList sanitizers: {
        var names = [];
        if (asan)
            names.push("address");
        if (ubsan)
            names.push("undefined");
        if (tsan)
            names.push("thread");
        return names;
    }

    // GCC and Clang agree on -fsanitize= and --coverage, but not on everything below.
    readonly property bool isClang: qbs.toolchain.contains("clang")

    // Passed to both the compiler and the linker driver: -fsanitize= and --coverage each select a
    // runtime library that has to be on the link line as well, and qbs feeds cpp.driverFlags to both.
    readonly property stringList instrumentationFlags: {
        var flags = [];

        for (var i = 0; i < sanitizers.length; ++i)
            flags.push("-fsanitize=" + sanitizers[i]);

        if (ubsan) {
            // The vptr check reads the RTTI a polymorphic object carries, and this project compiles
            // with cpp.enableRtti: false. Asking for both is at best ignored and at worst rejected by
            // the driver, so the check is dropped explicitly rather than left to the compiler.
            flags.push("-fno-sanitize=vptr");
        }

        if (anySanitizer) {
            // A sanitizer report is only as useful as the stack trace attached to it, and both of
            // these keep frames the optimizer would otherwise fold away. They cost nothing in the
            // debug builds the test scripts use, and keep a sanitizer build readable if someone
            // points one at an optimized configuration.
            flags.push("-fno-omit-frame-pointer");
            flags.push("-fno-optimize-sibling-calls");
        }

        if (coverage) {
            // --coverage is -fprofile-arcs -ftest-coverage when compiling and -lgcov when linking:
            // .gcno files land next to the object files at build time, and the matching .gcda files
            // are written back to those same paths when the binary runs.
            flags.push("--coverage");

            // This library is threaded throughout, and the default counter updates are not atomic:
            // two threads through the same arc lose increments, which shows up as lines that ran
            // being reported as cold. Correct counts are worth the slowdown here.
            flags.push("-fprofile-update=atomic");

            if (!isClang) {
                // Record source paths in the .gcno absolutely, so lcov and gcov resolve them from
                // any working directory. The build tree lives outside the source tree (output/),
                // which is exactly the case the relative default gets wrong.
                flags.push("-fprofile-abs-path");
            }
        }

        return flags;
    }

    cpp.driverFlags: instrumentationFlags

    // Let the code itself see how it was built: tests that race deliberately, or that measure timing,
    // need to know when they are running under an instrumented binary.
    cpp.defines: {
        var defs = [];
        if (asan)
            defs.push("KMX_AIO_SANITIZER_ASAN=1");
        if (ubsan)
            defs.push("KMX_AIO_SANITIZER_UBSAN=1");
        if (tsan)
            defs.push("KMX_AIO_SANITIZER_TSAN=1");
        if (coverage)
            defs.push("KMX_AIO_COVERAGE=1");
        if (faultInjection) {
            // The seam in aio/detail/syscalls.hpp compiles its faulting policy in only when this is
            // set. It belongs here for the same reason the sanitizer flags do: the library and the
            // test binary have to agree, or a test arms a fault that the library never consults.
            defs.push("KMX_AIO_FAULT_INJECTION=1");
        }
        return defs;
    }

    validate: {
        if (asan && tsan) {
            throw "project.enable_asan and project.enable_tsan cannot be combined: ASan and TSan "
                    + "ship mutually exclusive runtimes, and a binary can carry only one of them. "
                    + "Build and run the two configurations separately.";
        }
    }
}
