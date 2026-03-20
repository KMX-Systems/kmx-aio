import qbs 1.0

Project {
    // Pillar 1 feature gates. Keep all off by default so baseline builds stay portable.
    property bool enable_openonload: false
    property bool enable_af_xdp: false
    property bool enable_spdk: false

    references: [
        "library/lib.qbs",
        "library-test/unit-test.qbs",
        "sample/sample.qbs",
    ]
}
