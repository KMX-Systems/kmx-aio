import qbs

Project {
    references: [
        "common/sample-common.qbs",
        "client/sample-client.qbs",
        "server/sample-server.qbs",
        "simple-client/sample-simple-client.qbs",
        "simple-server/sample-simple-server.qbs",
        "simple-udp-client/sample-simple-udp-client.qbs",
        "simple-udp-server/sample-simple-udp-server.qbs",
    ]
}
