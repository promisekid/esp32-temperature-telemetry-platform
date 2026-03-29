#include "monitor_session.hpp"

#include <array>
#include <chrono>

namespace telemetry_platform::qt_monitor {

namespace {

std::uint64_t now_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

}  // namespace

bool MonitorSession::start(const MonitorSessionConfig &config, std::string *error_message) {
    stop();
    config_ = config;
    last_reconnect_attempt_ms_ = 0;

    decoder_.set_mode(config.mode);

    if (!config.replay_path.empty()) {
        common::ReplaySourceConfig replay_config {};
        replay_config.path = config.replay_path;
        replay_config.interval_ms = config.replay_interval_ms;

        if (!replay_source_.open(replay_config, error_message)) {
            return false;
        }

        if (config.mode == common::ProtocolMode::auto_detect) {
            decoder_.set_mode(replay_source_.protocol_hint());
        }
        return true;
    }

    serial_link::SerialPortConfig port_config {};
    port_config.port_name = config.port_name;
    port_config.baud_rate = config.baud_rate;
    port_config.read_timeout_ms = 10;

    return port_.open(port_config, error_message);
}

void MonitorSession::stop() {
    port_.close();
    replay_source_.close();
    last_reconnect_attempt_ms_ = 0;
}

bool MonitorSession::poll_once(std::string *error_message) {
    if (replay_source_.is_open()) {
        std::array<std::uint8_t, 2048> buffer {};
        std::string replay_error;
        const auto current_ms = now_ms();
        const auto bytes_read = replay_source_.read_some(buffer.data(), buffer.size(), current_ms, &replay_error);
        if (!replay_error.empty()) {
            if (error_message != nullptr) {
                *error_message = replay_error;
            }
            return false;
        }

        if (bytes_read > 0) {
            for (auto &packet : decoder_.push_bytes(buffer.data(), bytes_read)) {
                store_.apply(packet, current_ms);
            }
        }
        store_.evaluate_timeouts(current_ms);
        return true;
    }

    if (!port_.is_open()) {
        const auto current_ms = now_ms();
        if ((current_ms - last_reconnect_attempt_ms_) < 1000ULL) {
            return true;
        }

        serial_link::SerialPortConfig port_config {};
        port_config.port_name = config_.port_name;
        port_config.baud_rate = config_.baud_rate;
        port_config.read_timeout_ms = 10;

        last_reconnect_attempt_ms_ = current_ms;
        if (!port_.open(port_config, error_message)) {
            return false;
        }
    }

    std::array<std::uint8_t, 2048> buffer {};
    std::string read_error;
    const auto bytes_read = port_.read_some(buffer.data(), buffer.size(), &read_error);
    if (!read_error.empty()) {
        if (error_message != nullptr) {
            *error_message = read_error;
        }
        port_.close();
        last_reconnect_attempt_ms_ = now_ms();
        return false;
    }

    if (bytes_read > 0) {
        const auto current_ms = now_ms();
        for (auto &packet : decoder_.push_bytes(buffer.data(), bytes_read)) {
            store_.apply(packet, current_ms);
        }
        store_.evaluate_timeouts(current_ms);
    } else {
        store_.evaluate_timeouts(now_ms());
    }

    return true;
}

bool MonitorSession::request_device_config(std::string *error_message) {
    return write_command(packet_codec::encode_config_get(command_mode()), error_message);
}

bool MonitorSession::send_config_profile(const common::DeviceConfigProfile &profile, std::string *error_message) {
    return write_command(packet_codec::encode_config_set(profile, command_mode()), error_message);
}

common::DeviceState MonitorSession::snapshot() const {
    return store_.snapshot();
}

common::ProtocolMode MonitorSession::active_mode() const {
    return decoder_.active_mode();
}

bool MonitorSession::is_replay_mode() const {
    return replay_source_.is_open();
}

common::ProtocolMode MonitorSession::command_mode() const {
    auto mode = decoder_.active_mode();
    if (mode == common::ProtocolMode::auto_detect) {
        mode = decoder_.configured_mode();
    }
    if (mode == common::ProtocolMode::auto_detect) {
        mode = common::ProtocolMode::binary_v1;
    }
    return mode;
}

bool MonitorSession::write_command(const std::vector<std::uint8_t> &payload, std::string *error_message) {
    if (payload.empty()) {
        if (error_message != nullptr) {
            *error_message = "empty command payload";
        }
        return false;
    }

    if (replay_source_.is_open()) {
        if (error_message != nullptr) {
            *error_message = "command write is unavailable in replay mode";
        }
        return false;
    }

    if (!port_.is_open()) {
        serial_link::SerialPortConfig port_config {};
        port_config.port_name = config_.port_name;
        port_config.baud_rate = config_.baud_rate;
        port_config.read_timeout_ms = 10;

        if (!port_.open(port_config, error_message)) {
            return false;
        }
    }

    return port_.write_all(payload.data(), payload.size(), error_message);
}

}  // namespace telemetry_platform::qt_monitor
