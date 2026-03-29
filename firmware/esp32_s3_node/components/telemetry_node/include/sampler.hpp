#pragma once

#include "esp_err.h"
#include "telemetry_types.hpp"

namespace telemetry_node::sampler {

esp_err_t init();
void sampling_task(void *arg);
DeviceSnapshot get_latest_raw_snapshot();

}  // namespace telemetry_node::sampler
