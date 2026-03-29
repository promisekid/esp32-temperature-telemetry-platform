#include "command_rx.hpp"

#include "runtime_config.hpp"
#include "telemetry_uart.hpp"

#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace telemetry_node::command_rx {

namespace {

constexpr char kLogTag[] = "command_rx";
constexpr auto kConsoleUart = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);
constexpr std::uint8_t kMagic0 = 0xAAU;
constexpr std::uint8_t kMagic1 = 0x55U;
constexpr std::size_t kBinaryHeaderSize = 8;
constexpr std::size_t kBinaryFrameMinSize = 10;
constexpr std::size_t kConfigSetPayloadSize = 68;

std::vector<std::uint8_t> g_buffer;

esp_err_t ensure_uart_driver() {
    uart_config_t config {};
    config.baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;

    const auto config_result = uart_param_config(kConsoleUart, &config);
    if (config_result != ESP_OK) {
        ESP_LOGE(kLogTag, "uart_param_config failed: %s", esp_err_to_name(config_result));
        return config_result;
    }

    const auto install_result = uart_driver_install(kConsoleUart, 2048, 0, 0, nullptr, 0);
    if (install_result != ESP_OK && install_result != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kLogTag, "uart_driver_install failed: %s", esp_err_to_name(install_result));
        return install_result;
    }

    const auto flush_result = uart_flush_input(kConsoleUart);
    if (flush_result != ESP_OK) {
        ESP_LOGW(kLogTag, "uart_flush_input failed: %s", esp_err_to_name(flush_result));
    }

    return ESP_OK;
}

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

    auto value_pos = pos + token.size();
    while (value_pos < json.size() && std::isspace(static_cast<unsigned char>(json[value_pos])) != 0) {
        ++value_pos;
    }
    return value_pos;
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

std::optional<float> json_float_value(const std::string &json, const std::string &key) {
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

    std::string token = json.substr(start, end - start);
    char *parse_end = nullptr;
    const float value = std::strtof(token.c_str(), &parse_end);
    if (parse_end == nullptr || *parse_end != '\0') {
        return std::nullopt;
    }
    return value;
}

std::uint16_t read_u16_le(const std::uint8_t *data) {
    return static_cast<std::uint16_t>(data[0]) | (static_cast<std::uint16_t>(data[1]) << 8U);
}

std::int16_t read_i16_le(const std::uint8_t *data) {
    return static_cast<std::int16_t>(read_u16_le(data));
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

void copy_text(char *target, std::size_t size, const std::string &value) {
    std::snprintf(target, size, "%s", value.c_str());
}

ConfigFrame make_config_frame(bool persisted) {
    ConfigFrame frame {};
    frame.config = runtime_config::current();
    frame.persisted = persisted;
    return frame;
}

void emit_config(bool persisted) {
    uart::write_config(make_config_frame(persisted));
}

void emit_ack(const CommandAck &ack) {
    uart::write_command_ack(ack);
}

void handle_json_line(const std::string &raw_line) {
    const auto line = trim_copy(raw_line);
    if (line.empty()) {
        return;
    }

    const auto type = json_string_value(line, "type");
    if (!type.has_value()) {
        return;
    }

    if (*type == "config_get") {
        emit_config(true);
        return;
    }

    if (*type != "config_set") {
        return;
    }

    auto config = runtime_config::current();
    config.preferred_protocol =
        json_string_value(line, "preferred_protocol").value_or("binary-v1") == "binary-v1"
            ? ProtocolFormat::binary_v1
            : ProtocolFormat::jsonl_v2;
    config.sensor_gpio = json_integer_value<std::uint32_t>(line, "sensor_gpio").value_or(config.sensor_gpio);
    config.sample_period_ms =
        json_integer_value<std::uint32_t>(line, "sample_period_ms").value_or(config.sample_period_ms);
    config.heartbeat_period_ms =
        json_integer_value<std::uint32_t>(line, "heartbeat_period_ms").value_or(config.heartbeat_period_ms);
    copy_text(config.sensor_id, sizeof(config.sensor_id), json_string_value(line, "sensor_id").value_or(config.sensor_id));
    copy_text(
        config.firmware_version,
        sizeof(config.firmware_version),
        json_string_value(line, "fw_version").value_or(config.firmware_version)
    );
    config.valid_min_temp_c = json_float_value(line, "valid_min_temp_c").value_or(config.valid_min_temp_c);
    config.valid_max_temp_c = json_float_value(line, "valid_max_temp_c").value_or(config.valid_max_temp_c);
    config.real_overtemp_threshold_c =
        json_float_value(line, "real_overtemp_threshold_c").value_or(config.real_overtemp_threshold_c);
    config.simulated_min_temp_c =
        json_float_value(line, "simulated_min_temp_c").value_or(config.simulated_min_temp_c);
    config.simulated_max_temp_c =
        json_float_value(line, "simulated_max_temp_c").value_or(config.simulated_max_temp_c);
    config.simulated_step_c = json_float_value(line, "simulated_step_c").value_or(config.simulated_step_c);
    config.simulated_overtemp_threshold_c =
        json_float_value(line, "simulated_overtemp_threshold_c").value_or(config.simulated_overtemp_threshold_c);

    CommandAck ack {};
    runtime_config::apply(config, true, ack);
    emit_ack(ack);
    emit_config(true);
}

void handle_binary_frame(const std::vector<std::uint8_t> &frame) {
    if (frame.size() < kBinaryFrameMinSize) {
        return;
    }

    const auto type = frame[3];
    const auto payload_length = read_u16_le(frame.data() + 6);
    const auto *payload = frame.data() + kBinaryHeaderSize;

    if (type == 0x20U) {
        emit_config(true);
        return;
    }

    if (type != 0x21U || payload_length < kConfigSetPayloadSize) {
        return;
    }

    auto config = runtime_config::current();
    config.preferred_protocol = payload[0] == 1U ? ProtocolFormat::binary_v1 : ProtocolFormat::jsonl_v2;
    config.sensor_gpio = payload[1];
    config.sample_period_ms = read_u16_le(payload + 2);
    config.heartbeat_period_ms = read_u16_le(payload + 4);
    config.valid_min_temp_c = static_cast<float>(read_i16_le(payload + 6)) / 10.0F;
    config.valid_max_temp_c = static_cast<float>(read_i16_le(payload + 8)) / 10.0F;
    config.real_overtemp_threshold_c = static_cast<float>(read_i16_le(payload + 10)) / 10.0F;
    config.simulated_min_temp_c = static_cast<float>(read_i16_le(payload + 12)) / 10.0F;
    config.simulated_max_temp_c = static_cast<float>(read_i16_le(payload + 14)) / 10.0F;
    config.simulated_step_c = static_cast<float>(read_i16_le(payload + 16)) / 100.0F;
    config.simulated_overtemp_threshold_c = static_cast<float>(read_i16_le(payload + 18)) / 10.0F;
    std::snprintf(config.sensor_id, sizeof(config.sensor_id), "%s", reinterpret_cast<const char *>(payload + 20));
    std::snprintf(config.firmware_version, sizeof(config.firmware_version), "%s", reinterpret_cast<const char *>(payload + 44));

    CommandAck ack {};
    runtime_config::apply(config, true, ack);
    emit_ack(ack);
    emit_config(true);
}

void consume_json_lines() {
    while (true) {
        const auto newline = std::find(g_buffer.begin(), g_buffer.end(), static_cast<std::uint8_t>('\n'));
        if (newline == g_buffer.end()) {
            return;
        }

        std::string line(g_buffer.begin(), newline);
        g_buffer.erase(g_buffer.begin(), newline + 1);
        handle_json_line(line);
    }
}

void consume_binary_frames() {
    while (g_buffer.size() >= kBinaryFrameMinSize) {
        auto magic = g_buffer.begin();
        while (magic != g_buffer.end()) {
            const auto remaining = static_cast<std::size_t>(std::distance(magic, g_buffer.end()));
            if (remaining >= 2 && magic[0] == kMagic0 && magic[1] == kMagic1) {
                break;
            }
            ++magic;
        }
        if (magic == g_buffer.end()) {
            g_buffer.clear();
            return;
        }

        if (magic != g_buffer.begin()) {
            g_buffer.erase(g_buffer.begin(), magic);
        }

        if (g_buffer.size() < kBinaryFrameMinSize) {
            return;
        }

        const auto payload_length = read_u16_le(g_buffer.data() + 6);
        const auto frame_size = static_cast<std::size_t>(kBinaryHeaderSize + payload_length + 2U);
        if (g_buffer.size() < frame_size) {
            return;
        }

        const auto expected_crc = read_u16_le(g_buffer.data() + frame_size - 2);
        const auto actual_crc = crc16_ccitt_false(g_buffer.data() + 2, frame_size - 4);
        if (expected_crc != actual_crc) {
            g_buffer.erase(g_buffer.begin());
            continue;
        }

        std::vector<std::uint8_t> frame(g_buffer.begin(), g_buffer.begin() + static_cast<std::ptrdiff_t>(frame_size));
        g_buffer.erase(g_buffer.begin(), g_buffer.begin() + static_cast<std::ptrdiff_t>(frame_size));
        handle_binary_frame(frame);
    }
}

void consume_rx_buffer() {
    const auto first_meaningful = std::find_if(g_buffer.begin(), g_buffer.end(), [](std::uint8_t value) {
        return value != '\r' && value != '\n' && value != ' ' && value != '\t';
    });

    if (first_meaningful == g_buffer.end()) {
        g_buffer.clear();
        return;
    }

    if (first_meaningful != g_buffer.begin()) {
        g_buffer.erase(g_buffer.begin(), first_meaningful);
    }

    if (g_buffer.size() >= 2 && g_buffer[0] == kMagic0 && g_buffer[1] == kMagic1) {
        consume_binary_frames();
        return;
    }

    consume_json_lines();
}

}  // namespace

void command_task(void *arg) {
    (void)arg;

    if (ensure_uart_driver() != ESP_OK) {
        ESP_LOGE(kLogTag, "command RX disabled because UART driver init failed");
        vTaskDelete(nullptr);
        return;
    }

    std::uint8_t buffer[256] {};
    while (true) {
        const auto bytes_read = uart_read_bytes(kConsoleUart, buffer, sizeof(buffer), pdMS_TO_TICKS(50));
        if (bytes_read > 0) {
            g_buffer.insert(g_buffer.end(), buffer, buffer + bytes_read);
            consume_rx_buffer();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

}  // namespace telemetry_node::command_rx
