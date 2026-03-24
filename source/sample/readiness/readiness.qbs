import qbs

Project {
    references: [
        "tcp/echo/tcp-echo.qbs",
        "tcp/minimal/tcp-minimal.qbs",
        "udp/echo/udp-echo.qbs",
        "udp/minimal/udp-minimal.qbs",
        "tls/echo_readiness_server/tls-echo-readiness-server.qbs",
        "tls/echo_readiness_client/tls-echo-readiness-client.qbs",
    ]
}
