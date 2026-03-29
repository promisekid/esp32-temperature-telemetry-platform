add_library(telemetry_service_core STATIC
  src/telemetry_store.cpp
  src/telemetry_service_app.cpp
)

target_link_libraries(
  telemetry_service_core
  PUBLIC
    desktop_common
    serial_link
    packet_codec
)

target_include_directories(
  telemetry_service_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(telemetry_service
  src/main.cpp
)

target_link_libraries(
  telemetry_service
  PRIVATE
    telemetry_service_core
)
