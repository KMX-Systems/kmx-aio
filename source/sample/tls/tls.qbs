import qbs

Project {
    references: [
        "echo_readiness_server/tls-echo-readiness-server.qbs",
        "echo_completion_server/tls-echo-completion-server.qbs",
    ]
}
