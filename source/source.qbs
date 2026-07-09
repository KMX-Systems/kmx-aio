import qbs 1.0

Project {
    // Core remains always active; all other products are feature-gated here.
    property bool enable_readiness: false
    property bool enable_completion: true
    property bool enable_http2: false
    property bool enable_http3: false
    property bool enable_openonload: false
    property bool enable_af_xdp: false
    property bool enable_spdk: false
    property string spdk_prefix: sourceDirectory + "/../build/spdk-local/install-local"
    property bool spdk_enable_crypto: false
    property bool enable_quic: true
    property bool enable_avb: false
    property bool enable_opc_ua: false
    property bool opc_ua_vendored: true
    property string opc_ua_prefix: sourceDirectory + "/../build/open62541/install-local"
    property bool enable_cuda: false  // GPU support (requires CUDA toolkit installed)
    property bool enable_asan: false
    property bool enable_tsan: false

    references: [
        "library/library.qbs",
        "library-test/unit-test.qbs",
        "sample/sample.qbs",
    ]
}
