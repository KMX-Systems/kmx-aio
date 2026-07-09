import qbs

Project {
    references: [
        "common/common.qbs",
        "readiness/tcp/echo/common/tcp-echo-common.qbs",
        "readiness/readiness.qbs",
        "completion/completion.qbs",
        "gpu/gpu.qbs",
    ]
}

