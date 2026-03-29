add_library(desktop_common INTERFACE)

target_include_directories(
  desktop_common
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
