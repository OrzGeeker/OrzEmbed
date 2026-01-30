// swift-tools-version:5.10
import PackageDescription

let package = Package(
    name: "MyApp",
    dependencies: [
        // 依赖 ESP-IDF Swift 绑定（如果有）
    ],
    targets: [
        .executableTarget(
            name: "MyApp",
            path: "Sources/MyApp",
            linkerSettings: [
                // 链接 ESP-IDF 库
                .unsafeFlags(["-L/path/to/esp-idf/build/lib", "-lesp-idf"])
            ]
        )
    ]
)
