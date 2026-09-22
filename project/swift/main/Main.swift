// ESP32-C6 Embedded Swift 示例:GPIO 翻转
// 引脚号请按开发板调整(此处沿用工程其他示例的 GPIO2)
@_cdecl("app_main")
func main() {
  print("Hello from Swift on ESP32-C6!")

  var ledValue: Bool = false
  let blinkDelayMs: UInt32 = 500
  let led = Led(gpioPin: 2)

  while true {
    led.setLed(value: ledValue)
    ledValue.toggle()
    vTaskDelay(blinkDelayMs / (1000 / UInt32(configTICK_RATE_HZ)))
  }
}
