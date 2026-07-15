import qbs 1.0

Project {
    // Core remains always active; all other products are feature-gated here.
    // Aggregate toggles for enabling all optional feature gates at once.
    property bool full: false
    property bool all: false

    property bool enable_readiness: full || all || false
    property bool enable_completion: true
    property bool enable_http2: full || all || false
    property bool enable_http3: full || all || false
    property bool enable_openonload: full || all || false
    property bool enable_af_xdp: full || all || false
    property bool enable_spdk: full || all || false
    property string spdk_prefix: sourceDirectory + "/../build/spdk-local/install-local"
    property bool spdk_enable_crypto: false
    property bool enable_quic: full || all || false
    property bool enable_avb: full || all || false
    property bool enable_opc_ua: full || all || false
    property bool opc_ua_vendored: true
    property bool enable_modbus: false
    property string opc_ua_prefix: sourceDirectory + "/../build/open62541/install-local"
    property bool enable_someip: full || all || false
    property bool someip_vendored: true
    property bool someip_link_backend: false
    property string someip_prefix: sourceDirectory + "/../build/someip/install-local"
    property bool enable_cuda: full || all || false  // GPU support (requires CUDA toolkit installed)
    property bool enable_asan: false
    property bool enable_tsan: false

    references: [
        "library/library.qbs",
        "library-test/unit-test.qbs",
        "sample/sample.qbs",
    ]
}
