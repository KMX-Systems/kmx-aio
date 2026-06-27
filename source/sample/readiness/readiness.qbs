import qbs

Project {
    references: [
        //"tcp/echo/tcp-echo.qbs",
        //"tcp/minimal/tcp-minimal.qbs",
        "tcp/minimal/client/tcp-minimal-client.qbs",
        "tcp/minimal/server/tcp-minimal-server.qbs",
        //"udp/echo/udp-echo.qbs",
        "udp/echo/client/udp-echo-client.qbs",
        "udp/echo/server/udp-echo-server.qbs",
        "tls/echo_readiness_server/tls-echo-readiness-server.qbs",
        "tls/echo_readiness_client/tls-echo-readiness-client.qbs",
        "tls/h2_alpn_server/tls-h2-alpn-readiness-server.qbs",
        "tls/h2_alpn_client/tls-h2-alpn-readiness-client.qbs",
        "udp/minimal/client/udp-minimal-client.qbs",
        "udp/minimal/server/udp-minimal-server.qbs",
        "tcp/echo/common/tcp-echo-common.qbs",
        "tcp/echo/client/tcp-echo-client.qbs",
        "tcp/echo/server/tcp-echo-server.qbs",
        "v4l2/capture/v4l2-capture.qbs",
    ]
}
