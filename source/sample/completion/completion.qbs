import qbs

Project {
    condition: project.enable_completion
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
        "quic/echo-client/sample-quic-echo-client.qbs",
        "quic/echo-server/sample-quic-echo-server.qbs",
        "quic/http3-server/sample-quic-http3-server.qbs",
        "quic/http3-client/sample-quic-http3-client.qbs",
        "hft/order_router/sample-hft-order-router.qbs",
        "v4l2/capture/v4l2-completion-capture.qbs",
        "avb/talker/sample-avb-talker.qbs",
        "avb/listener/sample-avb-listener.qbs",
        "someip/echo-server/sample-someip-echo-server.qbs",
        "someip/echo-client/sample-someip-echo-client.qbs",
        "someip/event-publisher/sample-someip-event-publisher.qbs",
        "someip/event-subscriber/sample-someip-event-subscriber.qbs",
        "someip/diagnostics/sample-someip-diagnostics.qbs",
    ]
}
