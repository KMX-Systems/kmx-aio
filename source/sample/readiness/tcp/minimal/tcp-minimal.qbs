import qbs

Project {
    references: [
        "client/tcp-minimal-client.qbs",
        "server/tcp-minimal-server.qbs",
    ]
}
