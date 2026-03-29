#include "packet_codec.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>

namespace telemetry_platform::packet_codec {

namespace {

using telemetry_platform::common::ChannelSample;
using telemetry_platform::common::ChannelSource;
using telemetry_platform::common::ChannelStatus;
using telemetry_platform::common::DecodedPacket;
using telemetry_platform::common::DeviceHealth;
using telemetry_platform::common::DeviceSnapshot;
using telemetry_platform::common::FaultEvent;
using telemetry_platform::common::FaultFrame;
using telemetry_platform::common::HeartbeatFrame;
using telemetry_platform::common::ProtocolMode;
using telemetry_platform::common::TelemetryFrame;

constexpr std::uint8_t kMagic0 = 0xAAU;
constexpr std::uint8_t kMagic1 = 0x55U;
constexpr std::uint8_t kProtocolVersion = 0x01U;
constexpr std::size_t kBinaryHeaderSize = 8;
constexpr std::size_t kBinaryFrameMinSize = 10;
constexpr std::size_t kBinaryConfigPayloadSize = 69;
constexpr std::size_t kBinaryConfigSetPayloadSize = 68;
constexpr std::size_t kBinaryAckPayloadSize = 50;

std::string trim_copy(std::string value) {
    const auto left = value.find_first_not_of(" \t\r\n");
    if (left == std::string::npos) {
        return {};
    }
    const auto right = value.find_last_not_of(" \t\r\n");
    return value.substr(left, right - left + 1);
}

std::optional<std::size_t> find_key_offset(const std::string &json, const std::string &key) {
    const auto token = "\"" + key + "\":";
    const auto pos = json.find(token);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    return pos + token.size();
}

std::optional<std::string> json_string_value(const std::string &json, const std::string &key) {
    const auto value_pos = find_key_offset(json, key);
    if (!value_pos.has_value() || *value_pos >= json.size() || json[*value_pos] != '"') {
        return std::nullopt;
    }

    const auto start = *value_pos + 1;
    const auto end = json.find('"', start);
    if (end == std::string::npos) {
        return std::nullopt;
    }
    return json.substr(start, end - start);
}

template <typename IntegerType>
std::optional<IntegerType> json_integer_value(const std::string &json, const std::string &key) {
    const auto value_pos = find_key_offset(json, key);
    if (!value_pos.has_value()) {
        return std::nullopt;
    }

    const auto start = *value_pos;
    auto end = start;
    while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-')) {
        ++end;
    }

    if (end == start) {
        return std::nullopt;
    }

    IntegerType value {};
    const auto result = std::from_chars(json.data() + start, json.data() + end, value);
    if (result.ec != std::errc {}) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> json_double_value(const std::string &json, const std::string &key) {
    const auto value_pos = find_key_offset(json, key);
    if (!value_pos.has_value()) {
        return std::nullopt;
    }

    const auto start = *value_pos;
    auto end = start;
    while (end < json.size()) {
        const char ch = json[end];
        if (!(std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+' || ch == '.')) {
            break;
        }
        ++end;
    }

    if (end == start) {
        return std::nullopt;
    }

    try {
        return std::stod(json.substr(start, end - start));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> json_array_contents(const std::string &json, const std::string &key) {
    const auto value_pos = find_key_offset(json, key);
    if (!value_pos.has_value() || *value_pos >= json.size() || json[*value_pos] != '[') {
        return std::nullopt;
    }

    const auto start = *value_pos + 1;
    int depth = 1;
    for (std::size_t index = start; index < json.size(); ++index) {
        if (json[index] == '[') {
            ++depth;
        } else if (json[index] == ']') {
            --depth;
            if (depth == 0) {
                return json.substr(start, index - start);
            }
        }
    }

    return std::nullopt;
}

std::vector<std::string> split_json_objects(const std::string &value) {
    std::vector<std::string> objects;
    int depth = 0;
    std::size_t object_start = std::string::npos;

    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '{') {
            if (depth == 0) {
                object_start = index;
            }
            ++depth;
        } else if (value[index] == '}') {
            --depth;
            if (depth == 0 && object_start != std::string::npos) {
                objects.emplace_back(value.substr(object_start, index - object_start + 1));
                object_start = std::string::npos;
            }
        }
    }

    return objects;
}

std::uint16_t read_u16_le(const std::uint8_t *data) {
    return static_cast<std::uint16_t>(data[0]) | (static_cast<std::uint16_t>(data[1]) << 8U);
}

std::uint32_t read_u32_le(const std::uint8_t *data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::int16_t read_i16_le(const std::uint8_t *data) {
    return static_cast<std::int16_t>(read_u16_le(data));
}

void append_u8(std::vector<std::uint8_t> &buffer, std::uint8_t value) {
    buffer.push_back(value);
}

void append_u16_le(std::vector<std::uint8_t> &buffer, std::uint16_t value) {
    buffer.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    buffer.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void append_i16_le(std::vector<std::uint8_t> &buffer, std::int16_t value) {
    append_u16_le(buffer, static_cast<std::uint16_t>(value));
}

void append_u32_le(std::vector<std::uint8_t> &buffer, std::uint32_t value) {
    buffer.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    buffer.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    buffer.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    buffer.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

std::string trim_c_string(const std::uint8_t *data, std::size_t size) {
    std::string value(reinterpret_cast<const char *>(data), reinterpret_cast<const char *>(data + size));
    const auto zero = value.find('\0');
    if (zero != std::string::npos) {
        value.resize(zero);
    }
    return trim_copy(value);
}

void append_fixed_string(std::vector<std::uint8_t> &buffer, const std::string &value, std::size_t size) {
    const auto start = buffer.size();
    buffer.resize(start + size, 0U);
    const auto copy_size = std::min<std::size_t>(size == 0 ? 0 : size - 1, value.size());
    if (copy_size > 0) {
        std::memcpy(buffer.data() + start, value.data(), copy_size);
    }
}

std::uint8_t protocol_to_wire(common::ProtocolMode mode) {
    return mode == ProtocolMode::binary_v1 ? 1U : 0U;
}

common::ProtocolMode protocol_from_wire(std::uint8_t value) {
    return value == 1U ? ProtocolMode::binary_v1 : ProtocolMode::jsonl_v2;
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

ChannelSample make_channel_sample(
    std::uint8_t channel_id,
    ChannelSource source,
    std::string name,
    double temperature_c,
    ChannelStatus status
) {
    ChannelSample sample {};
    sample.channel_id = channel_id;
    sample.source = source;
    sample.name = name.empty() ? telemetry_platform::common::default_channel_name(channel_id, source) : std::move(name);
    sample.temperature_c = temperature_c;
    sample.status = status;
    return sample;
}

std::optional<DecodedPacket> decode_json_line(const std::string &line) {
    const auto trimmed = trim_copy(line);
    if (trimmed.empty() || trimmed.front() != '{') {
        return std::nullopt;
    }

    const auto type = json_string_value(trimmed, "type");
    if (!type.has_value()) {
        return std::nullopt;
    }

    const auto protocol_string = json_string_value(trimmed, "protocol").value_or("jsonl-v2");
    const auto protocol = telemetry_platform::common::protocol_mode_from_string(protocol_string).value_or(ProtocolMode::jsonl_v2);

    if (*type == "heartbeat") {
        HeartbeatFrame frame {};
        frame.protocol = protocol;
        frame.uptime_ms = json_integer_value<std::uint64_t>(trimmed, "uptime_ms").value_or(0);
        frame.fw_version = json_string_value(trimmed, "fw_version").value_or("");
        frame.channel_count = json_integer_value<std::uint32_t>(trimmed, "channel_count").value_or(1);
        frame.device_status = telemetry_platform::common::device_health_from_string(
                                  json_string_value(trimmed, "device_status").value_or("ok")
                              )
                                  .value_or(DeviceHealth::ok);
        return frame;
    }

    if (*type == "telemetry") {
        TelemetryFrame frame {};
        frame.protocol = protocol;
        frame.snapshot.sample_index = json_integer_value<std::uint32_t>(trimmed, "sample_index").value_or(0);
        frame.snapshot.uptime_ms = json_integer_value<std::uint64_t>(trimmed, "uptime_ms").value_or(0);

        const auto channels_value = json_array_contents(trimmed, "channels");
        if (channels_value.has_value()) {
            for (const auto &channel_json : split_json_objects(*channels_value)) {
                const auto channel_id = json_integer_value<std::uint32_t>(channel_json, "channel_id").value_or(0);
                const auto source = telemetry_platform::common::channel_source_from_string(
                                        json_string_value(channel_json, "source").value_or("real")
                                    )
                                        .value_or(ChannelSource::real);
                const auto name = json_string_value(channel_json, "name").value_or("");
                const auto temperature_c = json_double_value(channel_json, "temperature_c").value_or(0.0);
                const auto status = telemetry_platform::common::channel_status_from_string(
                                        json_string_value(channel_json, "status").value_or("not_found")
                                    )
                                        .value_or(ChannelStatus::not_found);

                frame.snapshot.channels.push_back(
                    make_channel_sample(static_cast<std::uint8_t>(channel_id), source, name, temperature_c, status)
                );
            }
        } else {
            frame.snapshot.channels.push_back(
                make_channel_sample(
                    0,
                    ChannelSource::real,
                    json_string_value(trimmed, "sensor_id").value_or("ds18b20_0"),
                    json_double_value(trimmed, "temperature_c").value_or(0.0),
                    telemetry_platform::common::channel_status_from_string(
                        json_string_value(trimmed, "status").value_or("not_found")
                    )
                        .value_or(ChannelStatus::not_found)
                )
            );
        }

        frame.snapshot.channel_count = static_cast<std::uint8_t>(frame.snapshot.channels.size());
        frame.snapshot.device_status = telemetry_platform::common::derive_device_health(frame.snapshot.channels);
        return frame;
    }

    if (*type == "fault") {
        FaultFrame frame {};
        frame.protocol = protocol;
        frame.fault.active = true;
        frame.fault.channel_id = static_cast<std::uint8_t>(
            json_integer_value<std::uint32_t>(trimmed, "channel_id").value_or(0)
        );
        frame.fault.source = telemetry_platform::common::channel_source_from_string(
                                 json_string_value(trimmed, "source").value_or("real")
                             )
                                 .value_or(ChannelSource::real);
        frame.fault.code = json_string_value(trimmed, "code").value_or("unknown_fault");
        frame.fault.fault_code = telemetry_platform::common::fault_code_from_name(frame.fault.code);
        frame.fault.message = json_string_value(trimmed, "message").value_or("");
        frame.fault.temperature_c = json_double_value(trimmed, "temperature_c").value_or(0.0);
        frame.fault.uptime_ms = json_integer_value<std::uint64_t>(trimmed, "uptime_ms").value_or(0);
        return frame;
    }

    if (*type == "config") {
        common::ConfigFrame frame {};
        frame.protocol = protocol;
        frame.persisted = json_integer_value<std::uint32_t>(trimmed, "persisted").value_or(0) != 0;
        frame.config.preferred_protocol = telemetry_platform::common::protocol_mode_from_string(
                                              json_string_value(trimmed, "preferred_protocol").value_or("binary-v1")
                                          )
                                              .value_or(ProtocolMode::binary_v1);
        frame.config.sensor_gpio = json_integer_value<std::uint32_t>(trimmed, "sensor_gpio").value_or(4);
        frame.config.sample_period_ms = json_integer_value<std::uint32_t>(trimmed, "sample_period_ms").value_or(1000);
        frame.config.heartbeat_period_ms = json_integer_value<std::uint32_t>(trimmed, "heartbeat_period_ms").value_or(5000);
        frame.config.sensor_id = json_string_value(trimmed, "sensor_id").value_or("ds18b20_0");
        frame.config.firmware_version = json_string_value(trimmed, "fw_version").value_or("m2-dev");
        frame.config.valid_min_temp_c = json_double_value(trimmed, "valid_min_temp_c").value_or(-55.0);
        frame.config.valid_max_temp_c = json_double_value(trimmed, "valid_max_temp_c").value_or(125.0);
        frame.config.real_overtemp_threshold_c = json_double_value(trimmed, "real_overtemp_threshold_c").value_or(30.0);
        frame.config.simulated_min_temp_c = json_double_value(trimmed, "simulated_min_temp_c").value_or(34.0);
        frame.config.simulated_max_temp_c = json_double_value(trimmed, "simulated_max_temp_c").value_or(38.0);
        frame.config.simulated_step_c = json_double_value(trimmed, "simulated_step_c").value_or(0.4);
        frame.config.simulated_overtemp_threshold_c =
            json_double_value(trimmed, "simulated_overtemp_threshold_c").value_or(37.0);
        return frame;
    }

    if (*type == "command_ack") {
        common::CommandAckFrame frame {};
        frame.protocol = protocol;
        frame.command = json_string_value(trimmed, "command").value_or("");
        frame.success = json_integer_value<std::uint32_t>(trimmed, "success").value_or(0) != 0;
        frame.message = json_string_value(trimmed, "message").value_or("");
        return frame;
    }

    return std::nullopt;
}

std::optional<DecodedPacket> decode_binary_frame(const std::vector<std::uint8_t> &frame) {
    if (frame.size() < kBinaryFrameMinSize) {
        return std::nullopt;
    }

    const auto message_type = frame[3];
    const auto payload_length = read_u16_le(frame.data() + 6);
    const auto *payload = frame.data() + kBinaryHeaderSize;

    if (message_type == 0x01U) {
        if (payload_length < 5) {
            return std::nullopt;
        }

        TelemetryFrame decoded {};
        decoded.protocol = ProtocolMode::binary_v1;
        decoded.snapshot.sample_index = read_u16_le(frame.data() + 4);
        decoded.snapshot.uptime_ms = read_u32_le(payload);
        decoded.snapshot.channel_count = payload[4];

        const auto expected_length = static_cast<std::size_t>(5 + decoded.snapshot.channel_count * 5U);
        if (payload_length < expected_length) {
            return std::nullopt;
        }

        std::size_t offset = 5;
        decoded.snapshot.channels.reserve(decoded.snapshot.channel_count);
        for (std::size_t index = 0; index < decoded.snapshot.channel_count; ++index) {
            const auto channel_id = payload[offset++];
            const auto source = static_cast<ChannelSource>(payload[offset++]);
            const auto status = static_cast<ChannelStatus>(payload[offset++]);
            const auto temperature_c = static_cast<double>(read_i16_le(payload + offset)) / 100.0;
            offset += 2;

            decoded.snapshot.channels.push_back(
                make_channel_sample(channel_id, source, "", temperature_c, status)
            );
        }

        decoded.snapshot.device_status = telemetry_platform::common::derive_device_health(decoded.snapshot.channels);
        return decoded;
    }

    if (message_type == 0x02U) {
        if (payload_length < 6) {
            return std::nullopt;
        }

        HeartbeatFrame decoded {};
        decoded.protocol = ProtocolMode::binary_v1;
        decoded.uptime_ms = read_u32_le(payload);
        decoded.channel_count = payload[4];
        decoded.device_status = static_cast<DeviceHealth>(payload[5]);
        return decoded;
    }

    if (message_type == 0x03U) {
        if (payload_length < 10) {
            return std::nullopt;
        }

        FaultFrame decoded {};
        decoded.protocol = ProtocolMode::binary_v1;
        decoded.fault.active = true;
        decoded.fault.channel_id = payload[0];
        decoded.fault.source = static_cast<ChannelSource>(payload[1]);
        decoded.fault.fault_code = read_u16_le(payload + 2);
        decoded.fault.code = telemetry_platform::common::fault_name_from_code(decoded.fault.fault_code);
        decoded.fault.temperature_c = static_cast<double>(read_i16_le(payload + 4)) / 100.0;
        decoded.fault.uptime_ms = read_u32_le(payload + 6);
        decoded.fault.message = decoded.fault.code;
        return decoded;
    }

    if (message_type == 0x10U) {
        if (payload_length < kBinaryConfigPayloadSize) {
            return std::nullopt;
        }

        common::ConfigFrame decoded {};
        decoded.protocol = ProtocolMode::binary_v1;
        decoded.config.preferred_protocol = protocol_from_wire(payload[0]);
        decoded.config.sensor_gpio = payload[1];
        decoded.config.sample_period_ms = read_u16_le(payload + 2);
        decoded.config.heartbeat_period_ms = read_u16_le(payload + 4);
        decoded.config.valid_min_temp_c = static_cast<double>(read_i16_le(payload + 6)) / 10.0;
        decoded.config.valid_max_temp_c = static_cast<double>(read_i16_le(payload + 8)) / 10.0;
        decoded.config.real_overtemp_threshold_c = static_cast<double>(read_i16_le(payload + 10)) / 10.0;
        decoded.config.simulated_min_temp_c = static_cast<double>(read_i16_le(payload + 12)) / 10.0;
        decoded.config.simulated_max_temp_c = static_cast<double>(read_i16_le(payload + 14)) / 10.0;
        decoded.config.simulated_step_c = static_cast<double>(read_i16_le(payload + 16)) / 100.0;
        decoded.config.simulated_overtemp_threshold_c = static_cast<double>(read_i16_le(payload + 18)) / 10.0;
        decoded.config.sensor_id = trim_c_string(payload + 20, 24);
        decoded.config.firmware_version = trim_c_string(payload + 44, 24);
        decoded.persisted = payload[68] != 0U;
        return decoded;
    }

    if (message_type == 0x11U) {
        if (payload_length < kBinaryAckPayloadSize) {
            return std::nullopt;
        }

        common::CommandAckFrame decoded {};
        decoded.protocol = ProtocolMode::binary_v1;
        decoded.success = payload[0] != 0U;
        decoded.command = payload[1] == 0x21U ? "config_set" : "config_get";
        decoded.message = trim_c_string(payload + 2, 48);
        return decoded;
    }

    return std::nullopt;
}

std::vector<std::uint8_t> encode_binary_frame(std::uint8_t message_type, const std::vector<std::uint8_t> &payload) {
    std::vector<std::uint8_t> frame;
    frame.reserve(kBinaryHeaderSize + payload.size() + 2U);

    append_u8(frame, kMagic0);
    append_u8(frame, kMagic1);
    append_u8(frame, kProtocolVersion);
    append_u8(frame, message_type);
    append_u16_le(frame, 0U);
    append_u16_le(frame, static_cast<std::uint16_t>(payload.size()));
    frame.insert(frame.end(), payload.begin(), payload.end());

    const auto crc = crc16_ccitt_false(frame.data() + 2, frame.size() - 2);
    append_u16_le(frame, crc);
    return frame;
}

std::vector<std::uint8_t> encode_binary_config_get() {
    return encode_binary_frame(0x20U, {});
}

std::vector<std::uint8_t> encode_binary_config_set(const common::DeviceConfigProfile &profile) {
    std::vector<std::uint8_t> payload;
    payload.reserve(kBinaryConfigSetPayloadSize);
    append_u8(payload, protocol_to_wire(profile.preferred_protocol));
    append_u8(payload, static_cast<std::uint8_t>(std::min<std::uint32_t>(profile.sensor_gpio, 255U)));
    append_u16_le(payload, static_cast<std::uint16_t>(profile.sample_period_ms));
    append_u16_le(payload, static_cast<std::uint16_t>(profile.heartbeat_period_ms));
    append_i16_le(payload, static_cast<std::int16_t>(std::lround(profile.valid_min_temp_c * 10.0)));
    append_i16_le(payload, static_cast<std::int16_t>(std::lround(profile.valid_max_temp_c * 10.0)));
    append_i16_le(payload, static_cast<std::int16_t>(std::lround(profile.real_overtemp_threshold_c * 10.0)));
    append_i16_le(payload, static_cast<std::int16_t>(std::lround(profile.simulated_min_temp_c * 10.0)));
    append_i16_le(payload, static_cast<std::int16_t>(std::lround(profile.simulated_max_temp_c * 10.0)));
    append_i16_le(payload, static_cast<std::int16_t>(std::lround(profile.simulated_step_c * 100.0)));
    append_i16_le(payload, static_cast<std::int16_t>(std::lround(profile.simulated_overtemp_threshold_c * 10.0)));
    append_fixed_string(payload, profile.sensor_id, 24);
    append_fixed_string(payload, profile.firmware_version, 24);
    return encode_binary_frame(0x21U, payload);
}

std::vector<std::uint8_t> encode_jsonl_command(const std::string &line) {
    return std::vector<std::uint8_t>(line.begin(), line.end());
}

}  // namespace

PacketDecoder::PacketDecoder(common::ProtocolMode mode) : configured_mode_(mode), active_mode_(mode) {}

void PacketDecoder::set_mode(common::ProtocolMode mode) {
    configured_mode_ = mode;
    active_mode_ = mode;
    buffer_.clear();
}

common::ProtocolMode PacketDecoder::configured_mode() const {
    return configured_mode_;
}

common::ProtocolMode PacketDecoder::active_mode() const {
    return active_mode_;
}

std::vector<common::DecodedPacket> PacketDecoder::push_bytes(const std::uint8_t *data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return {};
    }

    buffer_.insert(buffer_.end(), data, data + size);
    update_active_mode();

    if (active_mode_ == ProtocolMode::binary_v1) {
        return consume_binary_frames();
    }
    return consume_json_lines();
}

void PacketDecoder::update_active_mode() {
    if (configured_mode_ != ProtocolMode::auto_detect) {
        active_mode_ = configured_mode_;
        return;
    }

    const auto first_meaningful = std::find_if(buffer_.begin(), buffer_.end(), [](std::uint8_t value) {
        return value != '\r' && value != '\n' && value != ' ' && value != '\t';
    });

    if (first_meaningful == buffer_.end()) {
        active_mode_ = ProtocolMode::auto_detect;
        return;
    }

    const auto index = static_cast<std::size_t>(std::distance(buffer_.begin(), first_meaningful));
    if ((buffer_.size() - index) >= 2 && buffer_[index] == kMagic0 && buffer_[index + 1] == kMagic1) {
        active_mode_ = ProtocolMode::binary_v1;
        return;
    }

    active_mode_ = ProtocolMode::jsonl_v2;
}

std::vector<common::DecodedPacket> PacketDecoder::consume_json_lines() {
    std::vector<common::DecodedPacket> packets;

    while (true) {
        const auto newline = std::find(buffer_.begin(), buffer_.end(), static_cast<std::uint8_t>('\n'));
        if (newline == buffer_.end()) {
            break;
        }

        std::string line(buffer_.begin(), newline);
        buffer_.erase(buffer_.begin(), newline + 1);

        if (auto packet = decode_json_line(line); packet.has_value()) {
            packets.push_back(std::move(*packet));
        }
    }

    return packets;
}

std::vector<common::DecodedPacket> PacketDecoder::consume_binary_frames() {
    std::vector<common::DecodedPacket> packets;

    while (buffer_.size() >= kBinaryFrameMinSize) {
        auto magic = buffer_.begin();
        while (magic != buffer_.end()) {
            const auto remaining = static_cast<std::size_t>(std::distance(magic, buffer_.end()));
            if (remaining >= 2 && magic[0] == kMagic0 && magic[1] == kMagic1) {
                break;
            }
            ++magic;
        }
        if (magic == buffer_.end()) {
            buffer_.clear();
            break;
        }

        if (magic != buffer_.begin()) {
            buffer_.erase(buffer_.begin(), magic);
        }

        if (buffer_.size() < kBinaryFrameMinSize) {
            break;
        }

        const auto payload_length = read_u16_le(buffer_.data() + 6);
        const auto frame_size = static_cast<std::size_t>(kBinaryHeaderSize + payload_length + 2U);
        if (buffer_.size() < frame_size) {
            break;
        }

        const auto expected_crc = read_u16_le(buffer_.data() + frame_size - 2);
        const auto actual_crc = crc16_ccitt_false(buffer_.data() + 2, frame_size - 4);
        if (expected_crc != actual_crc) {
            buffer_.erase(buffer_.begin());
            continue;
        }

        std::vector<std::uint8_t> frame(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));

        if (auto packet = decode_binary_frame(frame); packet.has_value()) {
            packets.push_back(std::move(*packet));
        }
    }

    return packets;
}

std::vector<std::uint8_t> encode_config_get(common::ProtocolMode mode) {
    if (mode == common::ProtocolMode::binary_v1) {
        return encode_binary_config_get();
    }
    return encode_jsonl_command("{\"type\":\"config_get\",\"protocol\":\"jsonl-v2\"}\n");
}

std::vector<std::uint8_t> encode_config_set(const common::DeviceConfigProfile &profile, common::ProtocolMode mode) {
    if (mode == common::ProtocolMode::binary_v1) {
        return encode_binary_config_set(profile);
    }

    const auto line =
        std::string("{\"type\":\"config_set\",\"protocol\":\"jsonl-v2\",") +
        "\"preferred_protocol\":\"" + common::to_string(profile.preferred_protocol) + "\"," +
        "\"sensor_gpio\":" + std::to_string(profile.sensor_gpio) + "," +
        "\"sample_period_ms\":" + std::to_string(profile.sample_period_ms) + "," +
        "\"heartbeat_period_ms\":" + std::to_string(profile.heartbeat_period_ms) + "," +
        "\"sensor_id\":\"" + profile.sensor_id + "\"," +
        "\"fw_version\":\"" + profile.firmware_version + "\"," +
        "\"valid_min_temp_c\":" + std::to_string(profile.valid_min_temp_c) + "," +
        "\"valid_max_temp_c\":" + std::to_string(profile.valid_max_temp_c) + "," +
        "\"real_overtemp_threshold_c\":" + std::to_string(profile.real_overtemp_threshold_c) + "," +
        "\"simulated_min_temp_c\":" + std::to_string(profile.simulated_min_temp_c) + "," +
        "\"simulated_max_temp_c\":" + std::to_string(profile.simulated_max_temp_c) + "," +
        "\"simulated_step_c\":" + std::to_string(profile.simulated_step_c) + "," +
        "\"simulated_overtemp_threshold_c\":" + std::to_string(profile.simulated_overtemp_threshold_c) + "}\n";
    return encode_jsonl_command(line);
}

}  // namespace telemetry_platform::packet_codec
