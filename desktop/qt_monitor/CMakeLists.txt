cmake_minimum_required(VERSION 3.16)

project(QtMonitor LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(qt_monitor
  include/main_window.hpp
  include/monitor_session.hpp
  include/trend_widget.hpp
  src/main.cpp
  src/main_window.cpp
  src/monitor_session.cpp
  src/trend_widget.cpp
)

target_include_directories(
  qt_monitor
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
  qt_monitor
  PRIVATE
    Qt6::Widgets
    telemetry_service_core
)
