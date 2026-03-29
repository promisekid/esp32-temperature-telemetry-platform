#include "runtime_config.hpp"

#include "app_config.hpp"
#include "nvs.h"
#include "freertos/FreeRTOS.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace telemetry_node::runtime_config {

namespace {

constexpr char kNamespace[] = "telemetry";

RuntimeConfig g_current {};
portMUX_TYPE g_config_lock = portMUX_INITIALIZER_UNLOCKED;

std::int16_t scale10(float value) {
    return static_cast<std::int16_t>(std::lround(value * 10.0F));
}

std::int16_t scale100(float value) {
    return static_cast<std::int16_t>(std::lround(value * 100.0F));
}

float unscale10(std::int16_t value) {
    return static_cast<float>(value) / 10.0F;
}

float unscale100(std::int16_t value) {
    return static_cast<float>(value) / 100.0F;
}

void copy_text(char *target, std::size_t size, const char *value) {
    if (target == nullptr || size == 0) {
        return;
    }
    std::snprintf(target, size, "%s", value == nullptr ? "" : value);
}

RuntimeConfig make_defaults() {
    RuntimeConfig config {};
    config.preferred_protocol = app_config::protocol_format();
    config.sensor_gpio = static_cast<std::uint32_t>(app_config::sensor_gpio());
    config.sample_period_ms = app_config::sample_period_ms();
    config.heartbeat_period_ms = app_config::heartbeat_period_ms();
    copy_text(config.sensor_id, sizeof(config.sensor_id), app_config::sensor_id());
    copy_text(config.firmware_version, sizeof(config.firmware_version), app_config::fw_version());
    config.valid_min_temp_c = app_config::valid_min_temp_c();
    config.valid_max_temp_c = app_config::valid_max_temp_c();
    config.real_overtemp_threshold_c = app_config::real_channel_overtemp_threshold_c();
    config.simulated_min_temp_c = app_config::simulated_channel_min_temp_c();
    config.simulated_max_temp_c = app_config::simulated_channel_max_temp_c();
    config.simulated_step_c = app_config::simulated_channel_step_c();
    config.simulated_overtemp_threshold_c = app_config::simulated_channel_overtemp_threshold_c();
    return config;
}

bool load_from_nvs(RuntimeConfig &config) {
    nvs_handle_t handle {};
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    std::uint8_t protocol = static_cast<std::uint8_t>(config.preferred_protocol);
    std::uint32_t u32 = 0;
    std::int16_t i16 = 0;
    std::size_t text_size = 0;

    if (nvs_get_u8(handle, "protocol", &protocol) == ESP_OK) {
        config.preferred_protocol = protocol == 1U ? ProtocolFormat::binary_v1 : ProtocolFormat::jsonl_v2;
    }
    if (nvs_get_u32(handle, "sensor_gpio", &u32) == ESP_OK) {
        config.sensor_gpio = u32;
    }
    if (nvs_get_u32(handle, "sample_ms", &u32) == ESP_OK) {
        config.sample_period_ms = u32;
    }
    if (nvs_get_u32(handle, "heartbeat_ms", &u32) == ESP_OK) {
        config.heartbeat_period_ms = u32;
    }

    text_size = sizeof(config.sensor_id);
    nvs_get_str(handle, "sensor_id", config.sensor_id, &text_size);
    text_size = sizeof(config.firmware_version);
    nvs_get_str(handle, "fw_version", config.firmware_version, &text_size);

    if (nvs_get_i16(handle, "valid_min", &i16) == ESP_OK) {
        config.valid_min_temp_c = unscale10(i16);
    }
    if (nvs_get_i16(handle, "valid_max", &i16) == ESP_OK) {
        config.valid_max_temp_c = unscale10(i16);
    }
    if (nvs_get_i16(handle, "real_over", &i16) == ESP_OK) {
        config.real_overtemp_threshold_c = unscale10(i16);
    }
    if (nvs_get_i16(handle, "sim_min", &i16) == ESP_OK) {
        config.simulated_min_temp_c = unscale10(i16);
    }
    if (nvs_get_i16(handle, "sim_max", &i16) == ESP_OK) {
        config.simulated_max_temp_c = unscale10(i16);
    }
    if (nvs_get_i16(handle, "sim_step", &i16) == ESP_OK) {
        config.simulated_step_c = unscale100(i16);
    }
    if (nvs_get_i16(handle, "sim_over", &i16) == ESP_OK) {
        config.simulated_overtemp_threshold_c = unscale10(i16);
    }

    nvs_close(handle);
    return true;
}

esp_err_t save_to_nvs(const RuntimeConfig &config) {
    nvs_handle_t handle {};
    esp_err_t result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }

    result = nvs_set_u8(handle, "protocol", config.preferred_protocol == ProtocolFormat::binary_v1 ? 1U : 0U);
    if (result == ESP_OK) result = nvs_set_u32(handle, "sensor_gpio", config.sensor_gpio);
    if (result == ESP_OK) result = nvs_set_u32(handle, "sample_ms", config.sample_period_ms);
    if (result == ESP_OK) result = nvs_set_u32(handle, "heartbeat_ms", config.heartbeat_period_ms);
    if (result == ESP_OK) result = nvs_set_str(handle, "sensor_id", config.sensor_id);
    if (result == ESP_OK) result = nvs_set_str(handle, "fw_version", config.firmware_version);
    if (result == ESP_OK) result = nvs_set_i16(handle, "valid_min", scale10(config.valid_min_temp_c));
    if (result == ESP_OK) result = nvs_set_i16(handle, "valid_max", scale10(config.valid_max_temp_c));
    if (result == ESP_OK) result = nvs_set_i16(handle, "real_over", scale10(config.real_overtemp_threshold_c));
    if (result == ESP_OK) result = nvs_set_i16(handle, "sim_min", scale10(config.simulated_min_temp_c));
    if (result == ESP_OK) result = nvs_set_i16(handle, "sim_max", scale10(config.simulated_max_temp_c));
    if (result == ESP_OK) result = nvs_set_i16(handle, "sim_step", scale100(config.simulated_step_c));
    if (result == ESP_OK) result = nvs_set_i16(handle, "sim_over", scale10(config.simulated_overtemp_threshold_c));
    if (result == ESP_OK) result = nvs_commit(handle);

    nvs_close(handle);
    return result;
}

RuntimeConfig sanitize(RuntimeConfig config) {
    config.sensor_gpio = std::min<std::uint32_t>(config.sensor_gpio, 48U);
    config.sample_period_ms = std::clamp<std::uint32_t>(config.sample_period_ms, 500U, 5000U);
    config.heartbeat_period_ms = std::clamp<std::uint32_t>(config.heartbeat_period_ms, 1000U, 30000U);
    config.valid_min_temp_c = std::clamp(config.valid_min_temp_c, -100.0F, 150.0F);
    config.valid_max_temp_c = std::clamp(config.valid_max_temp_c, -100.0F, 200.0F);
    if (config.valid_max_temp_c < config.valid_min_temp_c) {
        std::swap(config.valid_min_temp_c, config.valid_max_temp_c);
    }
    config.real_overtemp_threshold_c = std::clamp(config.real_overtemp_threshold_c, -50.0F, 200.0F);
    config.simulated_min_temp_c = std::clamp(config.simulated_min_temp_c, -50.0F, 200.0F);
    config.simulated_max_temp_c = std::clamp(config.simulated_max_temp_c, -50.0F, 200.0F);
    if (config.simulated_max_temp_c < config.simulated_min_temp_c) {
        std::swap(config.simulated_min_temp_c, config.simulated_max_temp_c);
    }
    config.simulated_step_c = std::clamp(config.simulated_step_c, 0.1F, 20.0F);
    config.simulated_overtemp_threshold_c = std::clamp(config.simulated_overtemp_threshold_c, -50.0F, 200.0F);
    copy_text(config.sensor_id, sizeof(config.sensor_id), config.sensor_id);
    copy_text(config.firmware_version, sizeof(config.firmware_version), config.firmware_version);
    return config;
}

void fill_ack(CommandAck &ack, bool success, const char *command, const char *message) {
    ack.success = success;
    copy_text(ack.command, sizeof(ack.command), command);
    copy_text(ack.message, sizeof(ack.message), message);
}

}  // namespace

esp_err_t init() {
    auto config = sanitize(make_defaults());
    load_from_nvs(config);
    taskENTER_CRITICAL(&g_config_lock);
    g_current = sanitize(config);
    taskEXIT_CRITICAL(&g_config_lock);
    return ESP_OK;
}

RuntimeConfig defaults() {
    return sanitize(make_defaults());
}

RuntimeConfig current() {
    taskENTER_CRITICAL(&g_config_lock);
    const auto config = g_current;
    taskEXIT_CRITICAL(&g_config_lock);
    return config;
}

esp_err_t apply(const RuntimeConfig &config, bool persist, CommandAck &ack) {
    const auto sanitized = sanitize(config);

    taskENTER_CRITICAL(&g_config_lock);
    g_current = sanitized;
    taskEXIT_CRITICAL(&g_config_lock);

    if (persist) {
        const auto result = save_to_nvs(sanitized);
        if (result != ESP_OK) {
            fill_ack(ack, false, "config_set", "failed to persist config to nvs");
            return result;
        }
        fill_ack(ack, true, "config_set", "config applied and saved");
        return ESP_OK;
    }

    fill_ack(ack, true, "config_set", "config applied");
    return ESP_OK;
}

}  // namespace telemetry_node::runtime_config
