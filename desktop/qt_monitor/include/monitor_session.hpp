#pragma once

#include "packet_codec.hpp"
#include "replay_source.hpp"
#include "serial_link.hpp"
#include "telemetry_store.hpp"

#include <string>
#include <vector>

namespace telemetry_platform::qt_monitor {

struct MonitorSessionConfig {
    std::string port_name;
    std::string replay_path;
    std::uint32_t baud_rate {115200};
    std::uint32_t replay_interval_ms {250};
    common::ProtocolMode mode {common::ProtocolMode::auto_detect};
};

class MonitorSession {
  public:
    bool start(const MonitorSessionConfig &config, std::string *error_message = nullptr);
    void stop();
    bool poll_once(std::string *error_message = nullptr);
    bool request_device_config(std::string *error_message = nullptr);
    bool send_config_profile(const common::DeviceConfigProfile &profile, std::string *error_message = nullptr);

    [[nodiscard]] common::DeviceState snapshot() const;
    [[nodiscard]] common::ProtocolMode active_mode() const;
    [[nodiscard]] bool is_replay_mode() const;

  private:
    common::ProtocolMode command_mode() const;
    bool write_command(const std::vector<std::uint8_t> &payload, std::string *error_message);

    MonitorSessionConfig config_;
    std::uint64_t last_reconnect_attempt_ms_ {0};
    serial_link::SerialPort port_;
    common::ReplaySource replay_source_;
    packet_codec::PacketDecoder decoder_ {common::ProtocolMode::auto_detect};
    telemetry_service::TelemetryStore store_;
};

}  // namespace telemetry_platform::qt_monitor
