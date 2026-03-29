add_library(serial_link STATIC
  src/serial_link.cpp
)

target_link_libraries(serial_link PUBLIC desktop_common)

target_include_directories(
  serial_link
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
