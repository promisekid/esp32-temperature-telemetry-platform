#pragma once

#include "telemetry_types.hpp"

namespace telemetry_node::uart {

void write_heartbeat(const DeviceSnapshot &snapshot, const char *fw_version);
void write_telemetry(const DeviceSnapshot &snapshot);
void write_fault(const FaultEvent &fault);
void write_config(const ConfigFrame &config);
void write_command_ack(const CommandAck &ack);

}  // namespace telemetry_node::uart
