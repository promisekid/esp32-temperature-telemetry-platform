#include "sensor_driver.hpp"

#include "runtime_config.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

#include <cmath>
#include <cstdint>

namespace telemetry_node::sensor {

namespace {

constexpr char kLogTag[] = "ds18b20";
constexpr std::uint8_t kCommandSkipRom = 0xCC;
constexpr std::uint8_t kCommandConvertT = 0x44;
constexpr std::uint8_t kCommandReadScratchpad = 0xBE;
constexpr std::uint32_t kConversionDelayMs = 750;

gpio_num_t g_data_gpio = GPIO_NUM_NC;

void bus_drive_low() {
    gpio_set_direction(g_data_gpio, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(g_data_gpio, 0);
}

void bus_release() {
    gpio_set_direction(g_data_gpio, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(g_data_gpio, 1);
}

int bus_read_level() {
    return gpio_get_level(g_data_gpio);
}

bool one_wire_reset() {
    bus_drive_low();
    ets_delay_us(480);
    bus_release();
    ets_delay_us(70);
    const bool present = bus_read_level() == 0;
    ets_delay_us(410);
    return present;
}

void one_wire_write_bit(bool bit) {
    bus_drive_low();
    if (bit) {
        ets_delay_us(6);
        bus_release();
        ets_delay_us(64);
    } else {
        ets_delay_us(60);
        bus_release();
        ets_delay_us(10);
    }
}

bool one_wire_read_bit() {
    bus_drive_low();
    ets_delay_us(6);
    bus_release();
    ets_delay_us(9);
    const bool value = bus_read_level() != 0;
    ets_delay_us(55);
    return value;
}

void one_wire_write_byte(std::uint8_t value) {
    for (int i = 0; i < 8; ++i) {
        one_wire_write_bit((value >> i) & 0x01U);
    }
}

std::uint8_t one_wire_read_byte() {
    std::uint8_t value = 0;
    for (int i = 0; i < 8; ++i) {
        if (one_wire_read_bit()) {
            value |= static_cast<std::uint8_t>(1U << i);
        }
    }
    return value;
}

std::uint8_t crc8_dallas(const std::uint8_t *data, std::size_t length) {
    std::uint8_t crc = 0;
    for (std::size_t i = 0; i < length; ++i) {
        std::uint8_t inbyte = data[i];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const std::uint8_t mix = static_cast<std::uint8_t>((crc ^ inbyte) & 0x01U);
            crc >>= 1U;
            if (mix != 0U) {
                crc ^= 0x8CU;
            }
            inbyte >>= 1U;
        }
    }
    return crc;
}

ChannelStatus validate_temperature(float value) {
    const auto config = runtime_config::current();
    if (!std::isfinite(value)) {
        return ChannelStatus::read_error;
    }
    if (value < config.valid_min_temp_c || value > config.valid_max_temp_c) {
        return ChannelStatus::range_error;
    }
    return ChannelStatus::ok;
}

}  // namespace

esp_err_t reconfigure_gpio(gpio_num_t gpio) {
    if (g_data_gpio == gpio) {
        return ESP_OK;
    }

    g_data_gpio = gpio;
    gpio_config_t io_conf {};
    io_conf.pin_bit_mask = 1ULL << g_data_gpio;
    io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    bus_release();
    ESP_LOGI(kLogTag, "initialized ds18b20 driver on gpio=%d", g_data_gpio);
    return ESP_OK;
}

esp_err_t init() {
    return reconfigure_gpio(static_cast<gpio_num_t>(runtime_config::current().sensor_gpio));
}

ChannelStatus read_temperature(float &temperature_c) {
    temperature_c = 0.0F;

    if (!one_wire_reset()) {
        return ChannelStatus::not_found;
    }

    one_wire_write_byte(kCommandSkipRom);
    one_wire_write_byte(kCommandConvertT);
    vTaskDelay(pdMS_TO_TICKS(kConversionDelayMs));

    if (!one_wire_reset()) {
        return ChannelStatus::read_error;
    }

    one_wire_write_byte(kCommandSkipRom);
    one_wire_write_byte(kCommandReadScratchpad);

    std::uint8_t scratchpad[9] {};
    for (auto &byte : scratchpad) {
        byte = one_wire_read_byte();
    }

    if (crc8_dallas(scratchpad, 8) != scratchpad[8]) {
        return ChannelStatus::read_error;
    }

    const std::int16_t raw_temp = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(scratchpad[1] << 8U) | scratchpad[0]
    );
    temperature_c = static_cast<float>(raw_temp) / 16.0F;

    return validate_temperature(temperature_c);
}

const char *driver_name() {
    return "ds18b20";
}

}  // namespace telemetry_node::sensor
