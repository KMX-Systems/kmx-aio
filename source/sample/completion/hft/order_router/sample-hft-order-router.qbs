import qbs

CppApplication {
    name: "sample-hft-order-router"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
        "api",
        "inc_dep",
        "../../../../library/inc_dep",
    ]

    Depends { name: "kmx-aio-lib" }

    files: [
        "inc/kmx/aio/sample/hft/order_router/**.hpp",
        "src/kmx/aio/sample/hft/order_router/**.cpp",
        "src/main.cpp",
    ]
}
