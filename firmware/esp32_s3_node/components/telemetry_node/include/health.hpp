#pragma once

#include "telemetry_types.hpp"

namespace telemetry_node::health {

void init();
void diagnostics_task(void *arg);
DeviceSnapshot get_latest_snapshot();
bool take_pending_fault(FaultEvent &out_fault);

}  // namespace telemetry_node::health
