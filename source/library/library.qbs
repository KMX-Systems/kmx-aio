import qbs 1.0

Project {
    name: "kmx-aio-library"
    references: [
        "prj/core.qbs",
        "prj/readiness.qbs",
        "prj/completion.qbs",
        "prj/http2.qbs",
        "prj/http3.qbs",
        "prj/gpu.qbs",
        "prj/opcua.qbs",
        "prj/modbus.qbs",
        "prj/someip.qbs",
        "prj/quic.qbs",
        "prj/xdp.qbs",
        "prj/spdk.qbs",
        "prj/avb.qbs",
        "lib.qbs",
    ]
}
