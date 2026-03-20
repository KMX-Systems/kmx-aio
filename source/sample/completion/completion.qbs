import qbs

Project {
    references: [
        "tcp/echo_uring/server/tcp-echo-uring-server.qbs",
        "udp/echo_uring/server/udp-echo-uring-server.qbs",
        "tls/echo_completion_server/tls-echo-completion-server.qbs",
        "spdk/minimal/spdk-minimal.qbs",
        "spdk/discovery/spdk-discovery.qbs",
        "xdp/packet_filter/xdp-packet-filter.qbs",
    ]
}
