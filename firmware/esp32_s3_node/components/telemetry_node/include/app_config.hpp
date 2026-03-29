#pragma once

#include "telemetry_types.hpp"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include <cstdint>

namespace telemetry_node::app_config {

inline ProtocolFormat protocol_format() {
#if CONFIG_TELEMETRY_NODE_PROTOCOL_BINARY_V1
    return ProtocolFormat::binary_v1;
#else
    return ProtocolFormat::jsonl_v2;
#endif
}

inline gpio_num_t sensor_gpio() {
    return static_cast<gpio_num_t>(CONFIG_TELEMETRY_NODE_SENSOR_GPIO);
}

inline std::uint32_t sample_period_ms() {
    return CONFIG_TELEMETRY_NODE_SAMPLE_PERIOD_MS;
}

inline std::uint32_t heartbeat_period_ms() {
    return CONFIG_TELEMETRY_NODE_HEARTBEAT_PERIOD_MS;
}

inline const char *sensor_id() {
    return CONFIG_TELEMETRY_NODE_SENSOR_ID;
}

inline const char *fw_version() {
    return CONFIG_TELEMETRY_NODE_FW_VERSION;
}

inline const char *real_channel_name() {
    return CONFIG_TELEMETRY_NODE_SENSOR_ID;
}

inline const char *simulated_channel_name() {
    return "simulated_hot_channel";
}

inline float valid_min_temp_c() {
    return static_cast<float>(CONFIG_TELEMETRY_NODE_VALID_MIN_TEMP_C);
}

inline float valid_max_temp_c() {
    return static_cast<float>(CONFIG_TELEMETRY_NODE_VALID_MAX_TEMP_C);
}

inline std::uint64_t uptime_ms() {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

inline float real_channel_overtemp_threshold_c() {
    return 30.0F;
}

inline float simulated_channel_min_temp_c() {
    return 34.0F;
}

inline float simulated_channel_max_temp_c() {
    return 38.0F;
}

inline float simulated_channel_step_c() {
    return 0.4F;
}

inline float simulated_channel_overtemp_threshold_c() {
    return 37.0F;
}

}  // namespace telemetry_node::app_config
