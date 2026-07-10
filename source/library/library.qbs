import qbs 1.0

Project {
    references: [
        "core/core.qbs",
        "readiness/readiness.qbs",
        "completion/completion.qbs",
        "http2/http2.qbs",
        "http3/http3.qbs",
        "gpu/gpu.qbs",
        "opcua/opcua.qbs",
        "someip/someip.qbs",
        "quic/quic.qbs",
        "xdp/xdp.qbs",
        "spdk/spdk.qbs",
        "avb/avb.qbs",
        "lib.qbs",
    ]
}
