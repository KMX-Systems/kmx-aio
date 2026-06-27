import qbs

Project {
    references: [
        "client/udp-echo-client.qbs",
        "server/udp-echo-server.qbs",
    ]
}
