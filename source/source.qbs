import qbs 1.0

Project {
    // Pillar 1 feature gates. Most are enabled by default; OPC UA remains off by default.
    property bool enable_openonload: true
    property bool enable_af_xdp: true
    property bool enable_spdk: true
    property string spdk_prefix: "/usr/local"
    property bool spdk_enable_crypto: true
    property bool enable_quic: true
    property bool enable_avb: true
    property bool enable_opc_ua: false
    property bool opc_ua_vendored: false
    property string opc_ua_prefix: "/usr/local"
    property bool enable_cuda: false  // GPU support (requires CUDA toolkit installed)
    property bool enable_asan: false
    property bool enable_tsan: false

    references: [
        "library/library.qbs",
        "library-test/unit-test.qbs",
        "sample/sample.qbs",
    ]
}
