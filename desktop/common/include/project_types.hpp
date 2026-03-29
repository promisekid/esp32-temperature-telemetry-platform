#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace telemetry_platform::common {

enum class ProtocolMode : std::uint8_t {
    auto_detect = 0,
    jsonl_v2 = 1,
    binary_v1 = 2,
};

enum class ChannelSource : std::uint8_t {
    real = 0,
    simulated = 1,
};

enum class ChannelStatus : std::uint8_t {
    ok = 0,
    not_found = 1,
    read_error = 2,
    range_error = 3,
    overtemp = 4,
};

enum class DeviceHealth : std::uint8_t {
    ok = 0,
    fault_active = 1,
};

struct ChannelSample {
    std::uint8_t channel_id {0};
    ChannelSource source {ChannelSource::real};
    std::string name;
    double temperature_c {0.0};
    ChannelStatus status {ChannelStatus::not_found};
};

struct DeviceSnapshot {
    std::uint32_t sample_index {0};
    std::uint64_t uptime_ms {0};
    std::uint8_t channel_count {0};
    DeviceHealth device_status {DeviceHealth::ok};
    std::vector<ChannelSample> channels;
};

struct FaultEvent {
    bool active {false};
    std::uint8_t channel_id {0};
    ChannelSource source {ChannelSource::real};
    std::uint16_t fault_code {0};
    std::string code;
    std::string message;
    double temperature_c {0.0};
    std::uint64_t uptime_ms {0};
};

struct HeartbeatFrame {
    ProtocolMode protocol {ProtocolMode::jsonl_v2};
    std::uint64_t uptime_ms {0};
    std::string fw_version;
    std::uint8_t channel_count {0};
    DeviceHealth device_status {DeviceHealth::ok};
};

struct TelemetryFrame {
    ProtocolMode protocol {ProtocolMode::jsonl_v2};
    DeviceSnapshot snapshot;
};

struct FaultFrame {
    ProtocolMode protocol {ProtocolMode::jsonl_v2};
    FaultEvent fault;
};

struct DeviceConfigProfile {
    ProtocolMode preferred_protocol {ProtocolMode::binary_v1};
    std::uint32_t sensor_gpio {4};
    std::uint32_t sample_period_ms {1000};
    std::uint32_t heartbeat_period_ms {5000};
    std::string sensor_id {"ds18b20_0"};
    std::string firmware_version {"m2-dev"};
    double valid_min_temp_c {-55.0};
    double valid_max_temp_c {125.0};
    double real_overtemp_threshold_c {30.0};
    double simulated_min_temp_c {34.0};
    double simulated_max_temp_c {38.0};
    double simulated_step_c {0.4};
    double simulated_overtemp_threshold_c {37.0};
};

struct ConfigFrame {
    ProtocolMode protocol {ProtocolMode::jsonl_v2};
    DeviceConfigProfile config;
    bool persisted {false};
};

struct CommandAckFrame {
    ProtocolMode protocol {ProtocolMode::jsonl_v2};
    std::string command;
    bool success {false};
    std::string message;
};

using DecodedPacket = std::variant<HeartbeatFrame, TelemetryFrame, FaultFrame, ConfigFrame, CommandAckFrame>;

struct TrendPoint {
    std::uint64_t host_timestamp_ms {0};
    std::uint64_t device_uptime_ms {0};
    double temperature_c {0.0};
    ChannelStatus status {ChannelStatus::not_found};
};

struct DeviceState {
    bool online {false};
    std::string firmware_version;
    std::uint64_t last_heartbeat_host_ms {0};
    DeviceSnapshot latest_snapshot;
    std::vector<std::deque<TrendPoint>> trend_history;
    std::deque<FaultEvent> recent_faults;
    std::optional<DeviceConfigProfile> device_config;
    std::string last_command;
    std::string last_command_message;
    bool last_command_success {false};
};

inline const char *to_string(ProtocolMode mode) {
    switch (mode) {
        case ProtocolMode::auto_detect:
            return "auto";
        case ProtocolMode::jsonl_v2:
            return "jsonl-v2";
        case ProtocolMode::binary_v1:
            return "binary-v1";
        default:
            return "unknown";
    }
}

inline const char *to_string(ChannelSource source) {
    switch (source) {
        case ChannelSource::real:
            return "real";
        case ChannelSource::simulated:
            return "simulated";
        default:
            return "unknown";
    }
}

inline const char *to_string(ChannelStatus status) {
    switch (status) {
        case ChannelStatus::ok:
            return "ok";
        case ChannelStatus::not_found:
            return "not_found";
        case ChannelStatus::read_error:
            return "read_error";
        case ChannelStatus::range_error:
            return "range_error";
        case ChannelStatus::overtemp:
            return "overtemp";
        default:
            return "unknown";
    }
}

inline const char *to_string(DeviceHealth health) {
    switch (health) {
        case DeviceHealth::ok:
            return "ok";
        case DeviceHealth::fault_active:
            return "fault_active";
        default:
            return "unknown";
    }
}

inline std::optional<ProtocolMode> protocol_mode_from_string(const std::string &value) {
    if (value == "auto") {
        return ProtocolMode::auto_detect;
    }
    if (value == "jsonl-v2" || value == "jsonl" || value == "m1-jsonl") {
        return ProtocolMode::jsonl_v2;
    }
    if (value == "binary-v1" || value == "binary") {
        return ProtocolMode::binary_v1;
    }
    return std::nullopt;
}

inline std::optional<ChannelSource> channel_source_from_string(const std::string &value) {
    if (value == "real") {
        return ChannelSource::real;
    }
    if (value == "simulated") {
        return ChannelSource::simulated;
    }
    return std::nullopt;
}

inline std::optional<ChannelStatus> channel_status_from_string(const std::string &value) {
    if (value == "ok") {
        return ChannelStatus::ok;
    }
    if (value == "not_found") {
        return ChannelStatus::not_found;
    }
    if (value == "read_error") {
        return ChannelStatus::read_error;
    }
    if (value == "range_error") {
        return ChannelStatus::range_error;
    }
    if (value == "overtemp") {
        return ChannelStatus::overtemp;
    }
    return std::nullopt;
}

inline std::optional<DeviceHealth> device_health_from_string(const std::string &value) {
    if (value == "ok") {
        return DeviceHealth::ok;
    }
    if (value == "fault_active") {
        return DeviceHealth::fault_active;
    }
    return std::nullopt;
}

inline std::uint16_t fault_code_from_name(const std::string &value) {
    if (value == "sensor_not_found") {
        return 0x0001U;
    }
    if (value == "sensor_read_error") {
        return 0x0002U;
    }
    if (value == "temperature_out_of_range") {
        return 0x0003U;
    }
    if (value == "overtemp_warning") {
        return 0x0004U;
    }
    return 0x0000U;
}

inline std::string fault_name_from_code(std::uint16_t value) {
    switch (value) {
        case 0x0001U:
            return "sensor_not_found";
        case 0x0002U:
            return "sensor_read_error";
        case 0x0003U:
            return "temperature_out_of_range";
        case 0x0004U:
            return "overtemp_warning";
        default:
            return "unknown_fault";
    }
}

inline DeviceHealth derive_device_health(const std::vector<ChannelSample> &channels) {
    const auto fault_it = std::find_if(channels.begin(), channels.end(), [](const ChannelSample &channel) {
        return channel.status != ChannelStatus::ok;
    });
    return fault_it == channels.end() ? DeviceHealth::ok : DeviceHealth::fault_active;
}

inline std::string default_channel_name(std::uint8_t channel_id, ChannelSource source) {
    if (source == ChannelSource::real) {
        return channel_id == 0 ? "ds18b20_0" : "real_sensor";
    }
    return channel_id == 1 ? "simulated_hot_channel" : "simulated_channel";
}

inline DeviceConfigProfile default_device_config_profile() {
    return DeviceConfigProfile {};
}

}  // namespace telemetry_platform::common
