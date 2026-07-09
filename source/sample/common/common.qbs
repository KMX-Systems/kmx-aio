import qbs

StaticLibrary {
    Depends { name: "cpp" }

    name: "kmx-aio-sample-common"
    consoleApplication: true
    cpp.cxxLanguageVersion: "c++26"
    cpp.debugInformation: true
    cpp.enableRtti: false
    cpp.includePaths: [
        "inc",
    ]
    files: [
        "inc/kmx/aio/sample/common/**.hpp",
        "src/kmx/aio/sample/common/**.cpp",
    ]

    Export {
        Depends { name: "cpp" }
        cpp.includePaths: [ product.sourceDirectory + "/inc" ]
    }
}
