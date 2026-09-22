//! ESP32-C6 Rust 示例:GPIO 翻转 + 日志输出
//!
//! 目标:RISC-V(esp32c6),使用上游 nightly + build-std。
//! 注意:GPIO 引脚号需根据实际开发板调整。

use esp_idf_hal::delay::FreeRtos;
use esp_idf_hal::gpio::*;
use esp_idf_hal::peripherals::Peripherals;
use log::*;

fn main() {
    // 必须调用一次,否则 esp-idf-sys 注入的运行时补丁可能无法正确链接
    esp_idf_svc::sys::link_patches();

    // 将 log crate 绑定到 ESP-IDF 日志
    esp_idf_svc::log::EspLogger::initialize_default();

    info!("Hello, ESP32-C6 with Rust!");

    let peripherals = Peripherals::take().unwrap();

    // 根据开发板调整 GPIO 引脚
    let mut led = PinDriver::output(peripherals.pins.gpio2).unwrap();

    loop {
        info!("Turning LED on");
        led.set_high().unwrap();
        FreeRtos::delay_ms(1000);

        info!("Turning LED off");
        led.set_low().unwrap();
        FreeRtos::delay_ms(1000);
    }
}
