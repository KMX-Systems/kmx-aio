import qbs

Project {
    references: [
        "echo/tcp-echo.qbs",
        "echo_uring/server/tcp-echo-uring-server.qbs",
        "minimal/tcp-minimal.qbs",
    ]
}

