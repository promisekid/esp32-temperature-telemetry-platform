#pragma once

#include <array>
#include <cstdint>

namespace telemetry_node {

constexpr std::size_t kChannelCount = 2;
constexpr std::size_t kChannelNameLength = 24;
constexpr std::size_t kFaultCodeNameLength = 24;
constexpr std::size_t kFaultMessageLength = 64;
constexpr std::size_t kConfigTextLength = 24;
constexpr std::size_t kAckMessageLength = 64;

enum class ProtocolFormat : std::uint8_t {
    jsonl_v2 = 0,
    binary_v1 = 1,
};

enum class ChannelSource : std::uint8_t {
    real = 0,
    simulated = 1,
};

enum class ChannelStatus : std::uint8_t {
    ok = 0,
    not_found,
    read_error,
    range_error,
    overtemp,
};

enum class DeviceHealth : std::uint8_t {
    ok = 0,
    fault_active = 1,
};

struct ChannelSample {
    std::uint8_t channel_id {0};
    ChannelSource source {ChannelSource::real};
    char name[kChannelNameLength] {};
    float temperature_c {0.0F};
    ChannelStatus status {ChannelStatus::not_found};
};

struct DeviceSnapshot {
    std::uint32_t sample_index {0};
    std::uint64_t uptime_ms {0};
    std::uint8_t channel_count {static_cast<std::uint8_t>(kChannelCount)};
    DeviceHealth device_status {DeviceHealth::ok};
    std::array<ChannelSample, kChannelCount> channels {};
};

struct FaultEvent {
    bool active {false};
    std::uint8_t channel_id {0};
    ChannelSource source {ChannelSource::real};
    std::uint16_t fault_code {0};
    char code[kFaultCodeNameLength] {};
    char message[kFaultMessageLength] {};
    float temperature_c {0.0F};
    std::uint64_t uptime_ms {0};
};

struct RuntimeConfig {
    ProtocolFormat preferred_protocol {ProtocolFormat::binary_v1};
    std::uint32_t sensor_gpio {4};
    std::uint32_t sample_period_ms {1000};
    std::uint32_t heartbeat_period_ms {5000};
    char sensor_id[kConfigTextLength] {"ds18b20_0"};
    char firmware_version[kConfigTextLength] {"m2-dev"};
    float valid_min_temp_c {-55.0F};
    float valid_max_temp_c {125.0F};
    float real_overtemp_threshold_c {30.0F};
    float simulated_min_temp_c {34.0F};
    float simulated_max_temp_c {38.0F};
    float simulated_step_c {0.4F};
    float simulated_overtemp_threshold_c {37.0F};
};

struct ConfigFrame {
    RuntimeConfig config {};
    bool persisted {false};
};

struct CommandAck {
    bool success {false};
    char command[kConfigTextLength] {};
    char message[kAckMessageLength] {};
};

const char *channel_status_to_string(ChannelStatus status);
const char *channel_source_to_string(ChannelSource source);
const char *device_health_to_string(DeviceHealth health);
std::uint16_t fault_code_for_status(ChannelStatus status);
const char *fault_code_name_for_status(ChannelStatus status);
const char *fault_message_for_status(ChannelStatus status);

}  // namespace telemetry_node
