import qbs 1.0

Project {
    references: [
        "library/lib.qbs",
        "library-test/unit-test.qbs",
        "sample/client/sample-client.qbs",
        "sample/server/sample-server.qbs",
    ]
}
