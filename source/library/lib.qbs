import qbs

StaticLibrary {
    Depends { name: "cpp" }
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "inc_dep",
        "/usr/local/include",
    ]
    install: true
    name: "kmx-aio-lib"
    files: [
        // Root headers
        "inc/kmx/**.hpp",
        "inc/kmx/aio/**.hpp",

        // Descriptor headers
        "inc/kmx/aio/descriptor/**.hpp",

        // Readiness model headers
        "inc/kmx/aio/readiness/**.hpp",
        "inc/kmx/aio/readiness/tcp/**.hpp",
        "inc/kmx/aio/readiness/udp/**.hpp",
        "inc/kmx/aio/readiness/tls/**.hpp",
        "inc/kmx/aio/readiness/quic/**.hpp",

        // Completion model headers
        "inc/kmx/aio/completion/**.hpp",
        "inc/kmx/aio/completion/tcp/**.hpp",
        "inc/kmx/aio/completion/udp/**.hpp",
        "inc/kmx/aio/completion/tls/**.hpp",
        "inc/kmx/aio/completion/quic/**.hpp",
        "inc/kmx/aio/completion/xdp/**.hpp",

        // TCP protocol headers and sources (readiness base)
        "inc/kmx/aio/tcp/**.hpp",
        "src/kmx/aio/tcp/**.cpp",

        // UDP protocol headers and sources (readiness base)
        "inc/kmx/aio/udp/**.hpp",
        "src/kmx/aio/udp/**.cpp",

        // Descriptor sources
        "src/kmx/aio/descriptor/**.cpp",

        // Root sources (executor, scheduler, descriptor)
        "src/kmx/aio/**.cpp",

        // Completion model sources
        "src/kmx/aio/completion/**.cpp",
        "src/kmx/aio/completion/tcp/**.cpp",
        "src/kmx/aio/completion/udp/**.cpp",
        "src/kmx/aio/completion/tls/**.cpp",

        // Readiness model sources
        "src/kmx/aio/readiness/**.cpp",
        "src/kmx/aio/readiness/tls/**.cpp",
    ]
}
