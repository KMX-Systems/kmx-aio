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
        // Public headers
        "inc/kmx/**.hpp",

        // Core/runtime sources
        "src/kmx/aio/**.cpp",

        // Completion model sources
        "inc/kmx/aio/completion/**.hpp",
        "inc/kmx/aio/completion/tcp/**.hpp",
        "inc/kmx/aio/completion/udp/**.hpp",
        "inc/kmx/aio/completion/tls/**.hpp",
        "src/kmx/aio/completion/**.cpp",
        "src/kmx/aio/completion/tcp/**.cpp",
        "src/kmx/aio/completion/udp/**.cpp",
        "src/kmx/aio/completion/tls/**.cpp",

        // Readiness model sources
        "inc/kmx/aio/readiness/**.hpp",
        "inc/kmx/aio/readiness/descriptor/**.hpp",
        "inc/kmx/aio/readiness/tcp/**.hpp",
        "inc/kmx/aio/readiness/udp/**.hpp",
        "inc/kmx/aio/readiness/tls/**.hpp",
        "src/kmx/aio/readiness/**.cpp",
        "src/kmx/aio/readiness/descriptor/**.cpp",
        "src/kmx/aio/readiness/tcp/**.cpp",
        "src/kmx/aio/readiness/udp/**.cpp",
        "src/kmx/aio/readiness/tls/**.cpp",
    ]
}
