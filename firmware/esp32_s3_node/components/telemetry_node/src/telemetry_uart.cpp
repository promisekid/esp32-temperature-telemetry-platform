#include "telemetry_uart.hpp"

#include "runtime_config.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace telemetry_node::uart {

namespace {

constexpr std::uint8_t kMagic0 = 0xAAU;
constexpr std::uint8_t kMagic1 = 0x55U;
constexpr std::uint8_t kProtocolVersion = 0x01U;
constexpr std::size_t kMaxFrameSize = 128;

std::uint16_t g_packet_sequence = 0;

void write_line(const char *line) {
    std::printf("%s\n", line);
    std::fflush(stdout);
}

void write_bytes(const std::uint8_t *data, std::size_t size) {
    std::fwrite(data, 1, size, stdout);
    std::fflush(stdout);
}

int append_format(char *buffer, std::size_t capacity, std::size_t &offset, const char *format, ...) {
    if (offset >= capacity) {
        return -1;
    }

    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer + offset, capacity - offset, format, args);
    va_end(args);

    if (written < 0 || static_cast<std::size_t>(written) >= (capacity - offset)) {
        offset = capacity;
        return -1;
    }

    offset += static_cast<std::size_t>(written);
    return written;
}

void append_u8(std::uint8_t *buffer, std::size_t &offset, std::uint8_t value) {
    buffer[offset++] = value;
}

void append_u16_le(std::uint8_t *buffer, std::size_t &offset, std::uint16_t value) {
    buffer[offset++] = static_cast<std::uint8_t>(value & 0xFFU);
    buffer[offset++] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void append_u32_le(std::uint8_t *buffer, std::size_t &offset, std::uint32_t value) {
    buffer[offset++] = static_cast<std::uint8_t>(value & 0xFFU);
    buffer[offset++] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    buffer[offset++] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    buffer[offset++] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

void append_i16_le(std::uint8_t *buffer, std::size_t &offset, std::int16_t value) {
    append_u16_le(buffer, offset, static_cast<std::uint16_t>(value));
}

std::uint16_t crc16_ccitt_false(const std::uint8_t *data, std::size_t size) {
    std::uint16_t crc = 0xFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= static_cast<std::uint16_t>(data[index]) << 8U;
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U);
            } else {
                crc <<= 1U;
            }
        }
    }
    return crc;
}

void write_binary_frame(std::uint8_t message_type, const std::uint8_t *payload, std::size_t payload_size) {
    std::uint8_t frame[kMaxFrameSize] {};
    std::size_t offset = 0;

    append_u8(frame, offset, kMagic0);
    append_u8(frame, offset, kMagic1);
    append_u8(frame, offset, kProtocolVersion);
    append_u8(frame, offset, message_type);
    append_u16_le(frame, offset, g_packet_sequence++);
    append_u16_le(frame, offset, static_cast<std::uint16_t>(payload_size));

    if (payload_size > 0) {
        std::memcpy(frame + offset, payload, payload_size);
        offset += payload_size;
    }

    const std::uint16_t crc = crc16_ccitt_false(frame + 2, offset - 2);
    append_u16_le(frame, offset, crc);

    write_bytes(frame, offset);
}

void append_runtime_config_payload(std::uint8_t *payload, std::size_t &offset, const RuntimeConfig &config, bool persisted) {
    append_u8(payload, offset, config.preferred_protocol == ProtocolFormat::binary_v1 ? 1U : 0U);
    append_u8(payload, offset, static_cast<std::uint8_t>(config.sensor_gpio));
    append_u16_le(payload, offset, static_cast<std::uint16_t>(config.sample_period_ms));
    append_u16_le(payload, offset, static_cast<std::uint16_t>(config.heartbeat_period_ms));
    append_i16_le(payload, offset, static_cast<std::int16_t>(std::lround(config.valid_min_temp_c * 10.0F)));
    append_i16_le(payload, offset, static_cast<std::int16_t>(std::lround(config.valid_max_temp_c * 10.0F)));
    append_i16_le(payload, offset, static_cast<std::int16_t>(std::lround(config.real_overtemp_threshold_c * 10.0F)));
    append_i16_le(payload, offset, static_cast<std::int16_t>(std::lround(config.simulated_min_temp_c * 10.0F)));
    append_i16_le(payload, offset, static_cast<std::int16_t>(std::lround(config.simulated_max_temp_c * 10.0F)));
    append_i16_le(payload, offset, static_cast<std::int16_t>(std::lround(config.simulated_step_c * 100.0F)));
    append_i16_le(payload, offset, static_cast<std::int16_t>(std::lround(config.simulated_overtemp_threshold_c * 10.0F)));
    std::memcpy(payload + offset, config.sensor_id, kConfigTextLength);
    offset += kConfigTextLength;
    std::memcpy(payload + offset, config.firmware_version, kConfigTextLength);
    offset += kConfigTextLength;
    append_u8(payload, offset, persisted ? 1U : 0U);
}

void write_json_heartbeat(const DeviceSnapshot &snapshot, const char *fw_version) {
    char line[256] {};
    std::snprintf(
        line,
        sizeof(line),
        "{\"type\":\"heartbeat\",\"protocol\":\"jsonl-v2\",\"uptime_ms\":%llu,"
        "\"fw_version\":\"%s\",\"channel_count\":%u,\"device_status\":\"%s\"}",
        static_cast<unsigned long long>(snapshot.uptime_ms),
        fw_version,
        static_cast<unsigned>(snapshot.channel_count),
        device_health_to_string(snapshot.device_status)
    );
    write_line(line);
}

void write_json_telemetry(const DeviceSnapshot &snapshot) {
    char line[768] {};
    std::size_t offset = 0;

    append_format(
        line,
        sizeof(line),
        offset,
        "{\"type\":\"telemetry\",\"protocol\":\"jsonl-v2\",\"sample_index\":%lu,"
        "\"uptime_ms\":%llu,\"channels\":[",
        static_cast<unsigned long>(snapshot.sample_index),
        static_cast<unsigned long long>(snapshot.uptime_ms)
    );

    for (std::size_t index = 0; index < snapshot.channel_count; ++index) {
        const auto &channel = snapshot.channels[index];
        append_format(
            line,
            sizeof(line),
            offset,
            "%s{\"channel_id\":%u,\"source\":\"%s\",\"name\":\"%s\",\"temperature_c\":%.2f,"
            "\"status\":\"%s\"}",
            (index == 0) ? "" : ",",
            static_cast<unsigned>(channel.channel_id),
            channel_source_to_string(channel.source),
            channel.name,
            static_cast<double>(channel.temperature_c),
            channel_status_to_string(channel.status)
        );
    }

    append_format(line, sizeof(line), offset, "]}");
    write_line(line);
}

void write_json_fault(const FaultEvent &fault) {
    char line[320] {};
    std::snprintf(
        line,
        sizeof(line),
        "{\"type\":\"fault\",\"protocol\":\"jsonl-v2\",\"channel_id\":%u,\"source\":\"%s\","
        "\"code\":\"%s\",\"message\":\"%s\",\"uptime_ms\":%llu}",
        static_cast<unsigned>(fault.channel_id),
        channel_source_to_string(fault.source),
        fault.code,
        fault.message,
        static_cast<unsigned long long>(fault.uptime_ms)
    );
    write_line(line);
}

void write_json_config(const ConfigFrame &config) {
    char line[640] {};
    std::snprintf(
        line,
        sizeof(line),
        "{\"type\":\"config\",\"protocol\":\"jsonl-v2\",\"preferred_protocol\":\"%s\",\"sensor_gpio\":%lu,"
        "\"sample_period_ms\":%lu,\"heartbeat_period_ms\":%lu,\"sensor_id\":\"%s\",\"fw_version\":\"%s\","
        "\"valid_min_temp_c\":%.1f,\"valid_max_temp_c\":%.1f,\"real_overtemp_threshold_c\":%.1f,"
        "\"simulated_min_temp_c\":%.1f,\"simulated_max_temp_c\":%.1f,\"simulated_step_c\":%.2f,"
        "\"simulated_overtemp_threshold_c\":%.1f,\"persisted\":%u}",
        config.config.preferred_protocol == ProtocolFormat::binary_v1 ? "binary-v1" : "jsonl-v2",
        static_cast<unsigned long>(config.config.sensor_gpio),
        static_cast<unsigned long>(config.config.sample_period_ms),
        static_cast<unsigned long>(config.config.heartbeat_period_ms),
        config.config.sensor_id,
        config.config.firmware_version,
        static_cast<double>(config.config.valid_min_temp_c),
        static_cast<double>(config.config.valid_max_temp_c),
        static_cast<double>(config.config.real_overtemp_threshold_c),
        static_cast<double>(config.config.simulated_min_temp_c),
        static_cast<double>(config.config.simulated_max_temp_c),
        static_cast<double>(config.config.simulated_step_c),
        static_cast<double>(config.config.simulated_overtemp_threshold_c),
        config.persisted ? 1U : 0U
    );
    write_line(line);
}

void write_json_ack(const CommandAck &ack) {
    char line[320] {};
    std::snprintf(
        line,
        sizeof(line),
        "{\"type\":\"command_ack\",\"protocol\":\"jsonl-v2\",\"command\":\"%s\",\"success\":%u,\"message\":\"%s\"}",
        ack.command,
        ack.success ? 1U : 0U,
        ack.message
    );
    write_line(line);
}

void write_binary_heartbeat(const DeviceSnapshot &snapshot) {
    std::uint8_t payload[6] {};
    std::size_t offset = 0;
    append_u32_le(payload, offset, static_cast<std::uint32_t>(snapshot.uptime_ms));
    append_u8(payload, offset, snapshot.channel_count);
    append_u8(payload, offset, static_cast<std::uint8_t>(snapshot.device_status));
    write_binary_frame(0x02U, payload, offset);
}

void write_binary_telemetry(const DeviceSnapshot &snapshot) {
    std::uint8_t payload[32] {};
    std::size_t offset = 0;
    append_u32_le(payload, offset, static_cast<std::uint32_t>(snapshot.uptime_ms));
    append_u8(payload, offset, snapshot.channel_count);

    for (std::size_t index = 0; index < snapshot.channel_count; ++index) {
        const auto &channel = snapshot.channels[index];
        append_u8(payload, offset, channel.channel_id);
        append_u8(payload, offset, static_cast<std::uint8_t>(channel.source));
        append_u8(payload, offset, static_cast<std::uint8_t>(channel.status));
        append_i16_le(payload, offset, static_cast<std::int16_t>(std::lround(channel.temperature_c * 100.0F)));
    }

    write_binary_frame(0x01U, payload, offset);
}

void write_binary_fault(const FaultEvent &fault) {
    std::uint8_t payload[12] {};
    std::size_t offset = 0;
    append_u8(payload, offset, fault.channel_id);
    append_u8(payload, offset, static_cast<std::uint8_t>(fault.source));
    append_u16_le(payload, offset, fault.fault_code);
    append_i16_le(payload, offset, static_cast<std::int16_t>(std::lround(fault.temperature_c * 100.0F)));
    append_u32_le(payload, offset, static_cast<std::uint32_t>(fault.uptime_ms));
    write_binary_frame(0x03U, payload, offset);
}

void write_binary_config(const ConfigFrame &config) {
    std::uint8_t payload[80] {};
    std::size_t offset = 0;
    append_runtime_config_payload(payload, offset, config.config, config.persisted);
    write_binary_frame(0x10U, payload, offset);
}

void write_binary_ack(const CommandAck &ack) {
    std::uint8_t payload[64] {};
    std::size_t offset = 0;
    constexpr std::size_t kAckTextBytes = 48;
    append_u8(payload, offset, ack.success ? 1U : 0U);
    append_u8(payload, offset, std::strcmp(ack.command, "config_set") == 0 ? 0x21U : 0x20U);
    std::memcpy(payload + offset, ack.message, std::min<std::size_t>(kAckTextBytes, std::strlen(ack.message)));
    offset += kAckTextBytes;
    write_binary_frame(0x11U, payload, offset);
}

}  // namespace

void write_heartbeat(const DeviceSnapshot &snapshot, const char *fw_version) {
    if (runtime_config::current().preferred_protocol == ProtocolFormat::binary_v1) {
        write_binary_heartbeat(snapshot);
        return;
    }
    write_json_heartbeat(snapshot, fw_version);
}

void write_telemetry(const DeviceSnapshot &snapshot) {
    if (runtime_config::current().preferred_protocol == ProtocolFormat::binary_v1) {
        write_binary_telemetry(snapshot);
        return;
    }
    write_json_telemetry(snapshot);
}

void write_fault(const FaultEvent &fault) {
    if (!fault.active) {
        return;
    }

    if (runtime_config::current().preferred_protocol == ProtocolFormat::binary_v1) {
        write_binary_fault(fault);
        return;
    }
    write_json_fault(fault);
}

void write_config(const ConfigFrame &config) {
    if (runtime_config::current().preferred_protocol == ProtocolFormat::binary_v1) {
        write_binary_config(config);
        return;
    }
    write_json_config(config);
}

void write_command_ack(const CommandAck &ack) {
    if (runtime_config::current().preferred_protocol == ProtocolFormat::binary_v1) {
        write_binary_ack(ack);
        return;
    }
    write_json_ack(ack);
}

}  // namespace telemetry_node::uart
