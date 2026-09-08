import qbs

/*
    The optional-feature macros of this project, applied to the products that build with them.

    A feature macro decides whether a public header declares an API at all - <kmx/aio/modbus/client.hpp>
    is an empty file without KMX_AIO_FEATURE_MODBUS - so it has to mean the same thing in every
    translation unit that goes into one binary. Two translation units that disagree do not fail to
    build: one of them simply does not see the class, and the error arrives later as a missing symbol,
    or as no error at all and a feature that silently is not there.

    Repeating the list in every product is what makes that disagreement easy, and it had happened. Of
    the fifteen products that assembled a copy, kmx-aio-http3 assembled none, so <kmx/aio/quic/engine.hpp>
    was empty inside the library whose entire purpose is to sit on QUIC; kmx-aio-knx assembled only the
    KNX macros, so every other feature read as absent there; and READINESS, COMPLETION, HTTP2 and HTTP3
    were assembled by the benchmark alone, so the benchmark and the library it measures compiled the same
    headers into different APIs.

    So the set is decided once, in source/source.qbs, and a product joins in by depending on this module:

        Depends { name: "kmx_features" }

    Each library also re-exports the dependency, which is what carries the same set out to the samples,
    the benchmark and the unit-test binary without any of those repeating it.

    The module carries the include path of the generated <kmx/aio/config.hpp> along with the -D flags,
    and that pairing is deliberate. Public headers include that header, so a product that forgets to
    depend on this module fails to compile with "kmx/aio/config.hpp: No such file or directory" - which
    is the loud version of the failure this module exists to prevent, instead of the quiet one where the
    header is found, the macro is not defined, and the feature disappears.
*/
Module {
    Depends { name: "cpp" }

    // The macros, without the "=1" the command line wants. Named on the project so that
    // <kmx/aio/config.hpp> can be generated from the very same list.
    readonly property stringList macros: project.feature_macros

    // Where the generated <kmx/aio/config.hpp> was written. Reading it from the project is also what
    // forces it to have been written: the property is the output of the probe that writes it.
    readonly property string generatedIncludeDirectory: project.generated_include_dir

    cpp.defines: {
        var defines = [];
        for (var i = 0; i < macros.length; ++i)
            defines.push(macros[i] + "=1");
        return defines;
    }

    cpp.includePaths: [generatedIncludeDirectory]
}
