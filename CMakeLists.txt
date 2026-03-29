cmake_minimum_required(VERSION 3.16)

project(
  Esp32TelemetryDiagnosticsPlatform
  VERSION 0.1.0
  DESCRIPTION "ESP32-S3 telemetry, diagnostics, and Qt monitoring platform"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_DESKTOP "Build desktop services" ON)
option(BUILD_QT_MONITOR "Build Qt monitor application" OFF)

if(BUILD_DESKTOP)
  add_subdirectory(desktop)
endif()

if(BUILD_QT_MONITOR)
  add_subdirectory(desktop/qt_monitor)
endif()
