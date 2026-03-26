import qbs 1.0

Project {
    // Pillar 1 feature gates. Keep all off by default so baseline builds stay portable.
    property bool enable_openonload: true
    property bool enable_af_xdp: true
    property bool enable_spdk: true
    property bool enable_quic: true

    references: [
        "library/lib.qbs",
        "library-test/unit-test.qbs",
        "sample/sample.qbs",
    ]
}
