#pragma once

#include "telemetry_types.hpp"

#include "esp_err.h"

namespace telemetry_node::runtime_config {

esp_err_t init();
RuntimeConfig defaults();
RuntimeConfig current();
esp_err_t apply(const RuntimeConfig &config, bool persist, CommandAck &ack);

}  // namespace telemetry_node::runtime_config
