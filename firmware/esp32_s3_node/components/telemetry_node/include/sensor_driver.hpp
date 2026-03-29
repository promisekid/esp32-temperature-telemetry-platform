#pragma once

#include "esp_err.h"
#include "telemetry_types.hpp"
#include "driver/gpio.h"

namespace telemetry_node::sensor {

esp_err_t init();
esp_err_t reconfigure_gpio(gpio_num_t gpio);
ChannelStatus read_temperature(float &temperature_c);
const char *driver_name();

}  // namespace telemetry_node::sensor
