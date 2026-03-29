#include "sampler.hpp"

#include "app_config.hpp"
#include "esp_log.h"
#include "health.hpp"
#include "runtime_config.hpp"
#include "sensor_driver.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace telemetry_node::sampler {

namespace {

constexpr char kLogTag[] = "sampler";

DeviceSnapshot g_latest_raw_snapshot {};
SemaphoreHandle_t g_snapshot_mutex = nullptr;
std::uint32_t g_sample_index = 0;
float g_simulated_temperature_c = 0.0F;

ChannelSample make_channel_sample(
    std::uint8_t channel_id,
    ChannelSource source,
    const char *name,
    float temperature_c,
    ChannelStatus status
) {
    ChannelSample channel {};
    channel.channel_id = channel_id;
    channel.source = source;
    std::snprintf(channel.name, sizeof(channel.name), "%s", name);
    channel.temperature_c = temperature_c;
    channel.status = status;
    return channel;
}

void store_latest_snapshot(const DeviceSnapshot &snapshot) {
    if (g_snapshot_mutex == nullptr) {
        return;
    }

    if (xSemaphoreTake(g_snapshot_mutex, portMAX_DELAY) == pdTRUE) {
        g_latest_raw_snapshot = snapshot;
        xSemaphoreGive(g_snapshot_mutex);
    }
}

DeviceSnapshot make_initial_snapshot() {
    const auto config = runtime_config::current();
    DeviceSnapshot snapshot {};
    snapshot.sample_index = 0;
    snapshot.uptime_ms = app_config::uptime_ms();
    snapshot.channel_count = static_cast<std::uint8_t>(kChannelCount);
    snapshot.device_status = DeviceHealth::ok;
    snapshot.channels[0] = make_channel_sample(
        0,
        ChannelSource::real,
        config.sensor_id,
        0.0F,
        ChannelStatus::not_found
    );
    snapshot.channels[1] = make_channel_sample(
        1,
        ChannelSource::simulated,
        "simulated_hot_channel",
        config.simulated_min_temp_c,
        ChannelStatus::ok
    );
    return snapshot;
}

float next_simulated_temperature() {
    const auto config = runtime_config::current();
    const float current = g_simulated_temperature_c;
    g_simulated_temperature_c += config.simulated_step_c;
    if (g_simulated_temperature_c > config.simulated_max_temp_c) {
        g_simulated_temperature_c = config.simulated_min_temp_c;
    }
    return current;
}

}  // namespace

esp_err_t init() {
    g_snapshot_mutex = xSemaphoreCreateMutex();
    if (g_snapshot_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    g_simulated_temperature_c = runtime_config::current().simulated_min_temp_c;
    g_latest_raw_snapshot = make_initial_snapshot();
    g_sample_index = 0;

    return sensor::init();
}

void sampling_task(void *arg) {
    (void)arg;

    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        const auto config = runtime_config::current();
        DeviceSnapshot snapshot = make_initial_snapshot();
        snapshot.sample_index = ++g_sample_index;
        snapshot.uptime_ms = app_config::uptime_ms();

        sensor::reconfigure_gpio(static_cast<gpio_num_t>(config.sensor_gpio));

        float temperature_c = 0.0F;
        const auto real_status = sensor::read_temperature(temperature_c);
        snapshot.channels[0] = make_channel_sample(
            0,
            ChannelSource::real,
            config.sensor_id,
            temperature_c,
            real_status
        );
        snapshot.channels[1] = make_channel_sample(
            1,
            ChannelSource::simulated,
            "simulated_hot_channel",
            next_simulated_temperature(),
            ChannelStatus::ok
        );

        if (snapshot.channels[0].status == ChannelStatus::ok) {
            ESP_LOGD(
                kLogTag,
                "sample=%" PRIu32 " real=%.2f simulated=%.2f",
                snapshot.sample_index,
                snapshot.channels[0].temperature_c,
                snapshot.channels[1].temperature_c
            );
        } else {
            ESP_LOGW(
                kLogTag,
                "sample=%" PRIu32 " real_status=%s simulated=%.2f",
                snapshot.sample_index,
                channel_status_to_string(snapshot.channels[0].status),
                snapshot.channels[1].temperature_c
            );
        }

        store_latest_snapshot(snapshot);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(config.sample_period_ms));
    }
}

DeviceSnapshot get_latest_raw_snapshot() {
    if (g_snapshot_mutex == nullptr) {
        return make_initial_snapshot();
    }

    DeviceSnapshot snapshot {};
    if (xSemaphoreTake(g_snapshot_mutex, portMAX_DELAY) == pdTRUE) {
        snapshot = g_latest_raw_snapshot;
        xSemaphoreGive(g_snapshot_mutex);
    }
    return snapshot;
}

}  // namespace telemetry_node::sampler
