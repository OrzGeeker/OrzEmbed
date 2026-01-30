// 导入必要的模块
import Foundation

// 与 C 代码互操作
@_cdecl("app_main")
public func appMain() {
    print("Hello, ESP32-C6 with Swift!")
    
    // 主循环
    while true {
        print("Swift is running on ESP32-C6!")
        
        // 延时 1 秒
        usleep(1000000)
    }
}

// 必要的 C 兼容代码
@_silgen_name("main")
public func main(argc: Int32, argv: UnsafePointer<UnsafePointer<CChar>>?) -> Int32 {
    appMain()
    return 0
}
