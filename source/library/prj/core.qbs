import qbs

StaticLibrary {
    Depends { name: "cpp" }
    Depends { name: "kmx_instrumentation" }
    Depends { name: "kmx_features" }

    name: "kmx-aio-core"
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
        "../api/kmx/aio/allocator/counter.hpp",
        "../api/kmx/aio/allocator/slab.hpp",
        "../api/kmx/aio/allocator/statistics.hpp",
        "../api/kmx/aio/async_mutex.hpp",
        "../api/kmx/aio/bad_alloc.hpp",
        "../api/kmx/aio/basic_channel.hpp",
        "../api/kmx/aio/basic_types.hpp",
        "../api/kmx/aio/buffer/handle.hpp",
        "../api/kmx/aio/buffer/pool.hpp",
        "../api/kmx/aio/buffer/view/item.hpp",
        "../api/kmx/aio/channel.hpp",
        "../api/kmx/aio/error_code.hpp",
        "../api/kmx/aio/exception.hpp",
        "../api/kmx/aio/executor_base.hpp",
        "../api/kmx/aio/file_descriptor.hpp",
        "../api/kmx/aio/invalid_argument.hpp",
        "../api/kmx/aio/ipv4.hpp",
        "../api/kmx/aio/ipv6.hpp",
        "../api/kmx/aio/logic_error.hpp",
        "../api/kmx/aio/mac.hpp",
        "../api/kmx/aio/promise.hpp",
        "../api/kmx/aio/promise_base.hpp",
        "../api/kmx/aio/runtime_error.hpp",
        "../api/kmx/aio/scheduler.hpp",
        "../api/kmx/aio/stream_concepts.hpp",
        "../api/kmx/aio/system_error.hpp",
        "../api/kmx/aio/task.hpp",
        "../api/kmx/aio/tls/basic_stream.hpp",
        "../api/kmx/aio/tls/stream.hpp",
        "../inc/kmx/aio/allocator/detail/thread_state.hpp",
        "../inc/kmx/aio/detail/basic_syscalls.hpp",
        "../inc/kmx/aio/detail/fault_registry.hpp",
        "../inc/kmx/aio/detail/hex.hpp",
        "../inc/kmx/aio/detail/native_syscalls.hpp",
        "../inc/kmx/aio/detail/scope_exit.hpp",
        "../inc/kmx/aio/detail/scoped_fault.hpp",
        "../inc/kmx/aio/detail/syscalls.hpp",
        "../inc/kmx/aio/tls/detail/basic_openssl_syscalls.hpp",
        "../inc/kmx/aio/tls/detail/native_openssl_syscalls.hpp",
        "../inc/kmx/aio/tls/detail/tls_syscalls.hpp",
        "../src/kmx/aio/allocator/counter.cpp",
        "../src/kmx/aio/allocator/detail/thread_state.cpp",
        "../src/kmx/aio/allocator/slab.cpp",
        "../src/kmx/aio/allocator/statistics.cpp",
        "../src/kmx/aio/async_mutex.cpp",
        "../src/kmx/aio/basic_channel.cpp",
        "../src/kmx/aio/basic_types.cpp",
        "../src/kmx/aio/detail/native_syscalls.cpp",
        "../src/kmx/aio/error_code.cpp",
        "../src/kmx/aio/exception.cpp",
        "../src/kmx/aio/file_descriptor.cpp",
        "../src/kmx/aio/ipv4.cpp",
        "../src/kmx/aio/ipv6.cpp",
        "../src/kmx/aio/mac.cpp",
        "../src/kmx/aio/promise_base.cpp",
        "../src/kmx/aio/scheduler.cpp",
        "../src/kmx/aio/tls/basic_stream.cpp",
        "../src/kmx/aio/tls/detail/native_openssl_syscalls.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx_instrumentation" }
        Depends { name: "kmx_features" }
        cpp.includePaths: [ product.sourceDirectory + "/../api" ].concat(project.tls_include_paths)
    }
}
