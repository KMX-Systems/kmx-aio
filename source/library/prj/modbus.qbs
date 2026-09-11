import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx-aio-core" }
    Depends { name: "kmx-aio-readiness" }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }

    name: "kmx-aio-modbus"
    condition: project.enable_modbus
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "../api",
        "../inc",
        "/usr/local/include",
    ].concat(project.tls_include_paths)
    install: true
    files: [
        "../api/kmx/aio/modbus/**.hpp",
        "../inc/kmx/aio/modbus/**.hpp",
        "../inc/kmx/aio/modbus/detail/**.hpp",
        "../src/kmx/aio/modbus/detail/category.cpp",
        "../src/kmx/aio/modbus/error.cpp",
        "../src/kmx/aio/modbus/frame.cpp",
        "../src/kmx/aio/modbus/client.cpp",
        "../src/kmx/aio/modbus/server.cpp",
        "../src/kmx/aio/modbus/tls_client.cpp",
        "../src/kmx/aio/modbus/tls_server.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-aio-core" }
        Depends { name: "kmx-aio-readiness" }
        Depends { name: "kmx_instrumentation" }
        Depends { name: "kmx_features" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ].concat(project.tls_include_paths)
    }
}
