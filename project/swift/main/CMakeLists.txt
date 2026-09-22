# ==============================================================================
# ESP-IDF 组件配置(Embedded Swift)
# ==============================================================================
# SRCS 传 /dev/null:本组件没有 C/C++ 源文件,Swift 由 idf_component_register_swift() 编译
if(WIN32)
  set(devnull NUL)
else()
  set(devnull /dev/null)
endif()

idf_component_register(
    SRCS ${devnull}
    PRIV_INCLUDE_DIRS "."
    PRIV_REQUIRES idf_swift esp_driver_gpio
)

# idf_component_register_swift() 由 espressif/idf_swift 组件提供
idf_component_register_swift(
    ${COMPONENT_LIB}
    BRIDGING_HEADER ${CMAKE_CURRENT_LIST_DIR}/BridgingHeader.h
    SRCS Main.swift Led.swift
)
