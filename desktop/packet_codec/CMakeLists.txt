add_library(packet_codec STATIC
  src/packet_codec.cpp
)

target_link_libraries(packet_codec PUBLIC desktop_common)

target_include_directories(
  packet_codec
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
