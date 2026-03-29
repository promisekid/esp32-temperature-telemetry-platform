#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace telemetry_platform::serial_link {

struct SerialPortConfig {
    std::string port_name;
    std::uint32_t baud_rate {115200};
    std::uint32_t read_timeout_ms {100};
    bool assert_dtr {true};
    bool assert_rts {true};
};

class SerialPort {
  public:
    SerialPort();
    ~SerialPort();

    SerialPort(const SerialPort &) = delete;
    SerialPort &operator=(const SerialPort &) = delete;

    bool open(const SerialPortConfig &config, std::string *error_message = nullptr);
    void close();
    bool is_open() const;

    std::size_t read_some(std::uint8_t *buffer, std::size_t capacity, std::string *error_message = nullptr);
    bool write_all(const std::uint8_t *data, std::size_t size, std::string *error_message = nullptr);
    const SerialPortConfig &config() const;

  private:
    struct Impl;
    Impl *impl_;
    SerialPortConfig config_;
};

}  // namespace telemetry_platform::serial_link
