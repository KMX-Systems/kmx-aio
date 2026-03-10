import qbs

Project {
    references: [
        "common/tcp-echo-common.qbs",
        "client/tcp-echo-client.qbs",
        "server/tcp-echo-server.qbs",
    ]
}

