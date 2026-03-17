import qbs

Project {
    references: [
        "echo/udp-echo.qbs",
        "echo_uring/server/udp-echo-uring-server.qbs",
        "minimal/udp-minimal.qbs",
    ]
}

