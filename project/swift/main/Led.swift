// 对 ESP-IDF GPIO API 的简单 Swift 封装
struct Led {
  var ledPin: gpio_num_t

  init(gpioPin: Int) {
    ledPin = gpio_num_t(Int32(gpioPin))

    guard gpio_reset_pin(ledPin) == ESP_OK else {
      fatalError("cannot reset led")
    }
    guard gpio_set_direction(ledPin, GPIO_MODE_OUTPUT) == ESP_OK else {
      fatalError("cannot set led direction")
    }
  }

  func setLed(value: Bool) {
    let level: UInt32 = value ? 1 : 0
    gpio_set_level(ledPin, level)
  }
}
