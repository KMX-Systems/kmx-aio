import qbs

Project {
    references: [
        "tcp/echo_uring/server/tcp-echo-uring-server.qbs",
        "udp/echo_uring/server/udp-echo-uring-server.qbs",
        "tls/echo_completion_server/tls-echo-completion-server.qbs",
        "tls/echo_completion_client/tls-echo-completion-client.qbs",
        "tls/h2_alpn_client/tls-h2-alpn-client.qbs",
        "tls/h2_alpn_server/tls-h2-alpn-server.qbs",
        "spdk/minimal/spdk-minimal.qbs",
        "spdk/discovery/spdk-discovery.qbs",
        "xdp/packet_filter/xdp-packet-filter.qbs",
        "quic/echo-server/echo-server.qbs",
        "quic/http3-server/http3-server.qbs",
        "hft/order_router/hft-order-router.qbs",
    ]
}
