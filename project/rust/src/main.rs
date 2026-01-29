// 导入必要的库
use esp_idf_sys as _; // 链接 ESP-IDF
use esp_idf_hal::{delay::FreeRtos, gpio::*, prelude::Peripherals};
use log::*;

fn main() {
    // 初始化 ESP-IDF
    esp_idf_sys::link_patches();
    
    // 初始化日志
    esp_idf_svc::log::EspLogger::initialize_default();
    
    info!("Hello, ESP32-C6 with Rust!");
    
    // 获取外设
    let peripherals = Peripherals::take().unwrap();
    
    // 配置 LED 引脚（根据您的开发板调整引脚号）
    let mut led = PinDriver::output(peripherals.pins.gpio2).unwrap();
    
    // 主循环
    loop {
        info!("Turning LED on");
        led.set_high().unwrap();
        FreeRtos::delay_ms(1000);
        
        info!("Turning LED off");
        led.set_low().unwrap();
        FreeRtos::delay_ms(1000);
    }
}
