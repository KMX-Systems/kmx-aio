import qbs 1.0

Project {
    references: [
        "library/lib.qbs",
        "library-test/unit-test.qbs",
        "sample/common/sample-common.qbs",
        "sample/client/sample-client.qbs",
        "sample/server/sample-server.qbs",
        "sample/simple-client/sample-simple-client.qbs",
        "sample/simple-server/sample-simple-server.qbs",
        "sample/simple-udp-client/sample-simple-udp-client.qbs",
        "sample/simple-udp-server/sample-simple-udp-server.qbs",
    ]
}
