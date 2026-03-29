#include "app_config.hpp"
#include "command_rx.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "health.hpp"
#include "nvs_flash.h"
#include "runtime_config.hpp"
#include "sampler.hpp"
#include "telemetry_uart.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cinttypes>

namespace {

constexpr char kLogTag[] = "app_main";

const char *protocol_format_name() {
    return telemetry_node::runtime_config::current().preferred_protocol == telemetry_node::ProtocolFormat::binary_v1
        ? "binary-v1"
        : "jsonl-v2";
}

void protocol_tx_task(void *) {
    TickType_t last_wake = xTaskGetTickCount();
    std::uint64_t last_heartbeat_ms = 0;
    std::uint32_t last_sent_sample_index = 0;

    while (true) {
        const auto snapshot = telemetry_node::health::get_latest_snapshot();
        const auto uptime_ms = telemetry_node::app_config::uptime_ms();
        const auto config = telemetry_node::runtime_config::current();

        if (snapshot.sample_index != 0 && snapshot.sample_index != last_sent_sample_index) {
            telemetry_node::uart::write_telemetry(snapshot);
            last_sent_sample_index = snapshot.sample_index;
        }

        if ((uptime_ms - last_heartbeat_ms) >= config.heartbeat_period_ms) {
            telemetry_node::uart::write_heartbeat(snapshot, config.firmware_version);
            last_heartbeat_ms = uptime_ms;
        }

        telemetry_node::FaultEvent fault {};
        while (telemetry_node::health::take_pending_fault(fault)) {
            telemetry_node::uart::write_fault(fault);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(config.sample_period_ms));
    }
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kLogTag, "starting esp32 temperature telemetry node");

    auto nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);

    ESP_ERROR_CHECK(telemetry_node::runtime_config::init());
    ESP_ERROR_CHECK(telemetry_node::sampler::init());
    telemetry_node::health::init();

    const auto boot_config = telemetry_node::runtime_config::current();

    xTaskCreate(
        telemetry_node::sampler::sampling_task,
        "sample_task",
        4096,
        nullptr,
        5,
        nullptr
    );

    xTaskCreate(
        telemetry_node::health::diagnostics_task,
        "diag_task",
        4096,
        nullptr,
        4,
        nullptr
    );

    xTaskCreate(
        protocol_tx_task,
        "protocol_tx_task",
        4096,
        nullptr,
        3,
        nullptr
    );

    xTaskCreate(
        telemetry_node::command_rx::command_task,
        "command_rx_task",
        4096,
        nullptr,
        3,
        nullptr
    );

    ESP_LOGI(
        kLogTag,
        "sensor_gpio=%lu sample_period_ms=%lu heartbeat_period_ms=%lu protocol=%s",
        static_cast<unsigned long>(boot_config.sensor_gpio),
        static_cast<unsigned long>(boot_config.sample_period_ms),
        static_cast<unsigned long>(boot_config.heartbeat_period_ms),
        protocol_format_name()
    );
}
