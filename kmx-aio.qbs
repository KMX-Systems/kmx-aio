import qbs 1.0

Project {
    // The same search path source/source.qbs sets for itself, repeated because a sub-project's
    // qbsSearchPaths does not cover the products loaded through it from here. Both entry points into
    // this tree therefore have to name it: the build scripts resolve source/source.qbs directly, while
    // Qt Creator and anyone opening the repository root arrive through this file. Without the line
    // below, every product's Depends on kmx_instrumentation fails with "Dependency
    // 'kmx_instrumentation' not found", which disables kmx-aio-core and, in turn, the whole tree.
    qbsSearchPaths: [sourceDirectory + "/source/qbs"]

    references: [
        "source/source.qbs"
    ]
}
