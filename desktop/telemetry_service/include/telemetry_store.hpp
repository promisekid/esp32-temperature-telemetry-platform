#pragma once

#include "project_types.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace telemetry_platform::telemetry_service {

struct TelemetryStoreConfig {
    std::uint64_t trend_window_ms {120000};
    std::size_t max_faults {200};
    std::uint64_t offline_timeout_ms {12000};
};

class TelemetryStore {
  public:
    explicit TelemetryStore(TelemetryStoreConfig config = {});

    void apply(const common::DecodedPacket &packet, std::uint64_t host_timestamp_ms);
    void evaluate_timeouts(std::uint64_t host_timestamp_ms);
    common::DeviceState snapshot() const;

  private:
    void apply_heartbeat(const common::HeartbeatFrame &packet, std::uint64_t host_timestamp_ms);
    void apply_telemetry(const common::TelemetryFrame &packet, std::uint64_t host_timestamp_ms);
    void apply_fault(const common::FaultFrame &packet);
    void apply_config(const common::ConfigFrame &packet);
    void apply_command_ack(const common::CommandAckFrame &packet);
    void trim_history_locked(std::uint64_t now_ms);
    void ensure_channel_capacity_locked(std::size_t count);

    TelemetryStoreConfig config_;
    mutable std::mutex mutex_;
    common::DeviceState state_;
};

}  // namespace telemetry_platform::telemetry_service
