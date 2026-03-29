idf_component_register(
  SRCS
    "src/command_rx.cpp"
    "src/health.cpp"
    "src/runtime_config.cpp"
    "src/sampler.cpp"
    "src/sensor_ds18b20.cpp"
    "src/telemetry_uart.cpp"
  INCLUDE_DIRS "include"
  REQUIRES driver esp_timer nvs_flash
)
