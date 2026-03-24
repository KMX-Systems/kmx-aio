import qbs

Project {
    references: [
        "tcp/echo/tcp-echo.qbs",
        "tcp/minimal/tcp-minimal.qbs",
        "udp/echo/udp-echo.qbs",
        "udp/minimal/udp-minimal.qbs",
        "tls/echo_readiness_server/tls-echo-readiness-server.qbs",
        "tls/echo_readiness_client/tls-echo-readiness-client.qbs",
        "tls/h2_alpn_server/tls-h2-alpn-readiness-server.qbs",
        "tls/h2_alpn_client/tls-h2-alpn-readiness-client.qbs",
    ]
}
