#include "telemetry_store.hpp"

#include <algorithm>
#include <type_traits>
#include <variant>

namespace telemetry_platform::telemetry_service {

TelemetryStore::TelemetryStore(TelemetryStoreConfig config) : config_(config) {}

void TelemetryStore::apply(const common::DecodedPacket &packet, std::uint64_t host_timestamp_ms) {
    std::visit(
        [this, host_timestamp_ms](const auto &typed_packet) {
            using PacketType = std::decay_t<decltype(typed_packet)>;
            if constexpr (std::is_same_v<PacketType, common::HeartbeatFrame>) {
                apply_heartbeat(typed_packet, host_timestamp_ms);
            } else if constexpr (std::is_same_v<PacketType, common::TelemetryFrame>) {
                apply_telemetry(typed_packet, host_timestamp_ms);
            } else if constexpr (std::is_same_v<PacketType, common::FaultFrame>) {
                apply_fault(typed_packet);
            } else if constexpr (std::is_same_v<PacketType, common::ConfigFrame>) {
                apply_config(typed_packet);
            } else if constexpr (std::is_same_v<PacketType, common::CommandAckFrame>) {
                apply_command_ack(typed_packet);
            }
        },
        packet
    );
}

void TelemetryStore::evaluate_timeouts(std::uint64_t host_timestamp_ms) {
    std::scoped_lock lock(mutex_);
    if (state_.last_heartbeat_host_ms == 0) {
        state_.online = false;
        return;
    }

    if ((host_timestamp_ms - state_.last_heartbeat_host_ms) > config_.offline_timeout_ms) {
        state_.online = false;
    }
    trim_history_locked(host_timestamp_ms);
}

common::DeviceState TelemetryStore::snapshot() const {
    std::scoped_lock lock(mutex_);
    return state_;
}

void TelemetryStore::apply_heartbeat(const common::HeartbeatFrame &packet, std::uint64_t host_timestamp_ms) {
    std::scoped_lock lock(mutex_);
    state_.online = true;
    state_.last_heartbeat_host_ms = host_timestamp_ms;
    state_.firmware_version = packet.fw_version.empty() ? state_.firmware_version : packet.fw_version;

    if (state_.latest_snapshot.channel_count == 0) {
        state_.latest_snapshot.channel_count = packet.channel_count;
    }
    state_.latest_snapshot.uptime_ms = packet.uptime_ms;
    state_.latest_snapshot.device_status = packet.device_status;

    ensure_channel_capacity_locked(std::max<std::size_t>(packet.channel_count, state_.latest_snapshot.channels.size()));
}

void TelemetryStore::apply_telemetry(const common::TelemetryFrame &packet, std::uint64_t host_timestamp_ms) {
    std::scoped_lock lock(mutex_);
    state_.latest_snapshot = packet.snapshot;
    ensure_channel_capacity_locked(packet.snapshot.channels.size());

    for (const auto &channel : packet.snapshot.channels) {
        if (channel.channel_id >= state_.trend_history.size()) {
            continue;
        }

        state_.trend_history[channel.channel_id].push_back(
            common::TrendPoint {
                host_timestamp_ms,
                packet.snapshot.uptime_ms,
                channel.temperature_c,
                channel.status,
            }
        );
    }

    trim_history_locked(host_timestamp_ms);
}

void TelemetryStore::apply_fault(const common::FaultFrame &packet) {
    std::scoped_lock lock(mutex_);
    state_.recent_faults.push_front(packet.fault);
    while (state_.recent_faults.size() > config_.max_faults) {
        state_.recent_faults.pop_back();
    }
}

void TelemetryStore::apply_config(const common::ConfigFrame &packet) {
    std::scoped_lock lock(mutex_);
    state_.device_config = packet.config;
}

void TelemetryStore::apply_command_ack(const common::CommandAckFrame &packet) {
    std::scoped_lock lock(mutex_);
    state_.last_command = packet.command;
    state_.last_command_success = packet.success;
    state_.last_command_message = packet.message;
}

void TelemetryStore::trim_history_locked(std::uint64_t now_ms) {
    for (auto &channel_history : state_.trend_history) {
        while (!channel_history.empty() &&
               (now_ms - channel_history.front().host_timestamp_ms) > config_.trend_window_ms) {
            channel_history.pop_front();
        }
    }
}

void TelemetryStore::ensure_channel_capacity_locked(std::size_t count) {
    if (state_.trend_history.size() < count) {
        state_.trend_history.resize(count);
    }
}

}  // namespace telemetry_platform::telemetry_service
