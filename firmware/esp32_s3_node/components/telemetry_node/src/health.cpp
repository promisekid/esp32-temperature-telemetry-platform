#include "health.hpp"

#include "app_config.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "runtime_config.hpp"
#include "sampler.hpp"

#include <array>
#include <cstdio>
#include <cstring>

namespace telemetry_node {

const char *channel_status_to_string(ChannelStatus status) {
    switch (status) {
        case ChannelStatus::ok:
            return "ok";
        case ChannelStatus::not_found:
            return "not_found";
        case ChannelStatus::read_error:
            return "read_error";
        case ChannelStatus::range_error:
            return "range_error";
        case ChannelStatus::overtemp:
            return "overtemp";
        default:
            return "unknown";
    }
}

const char *channel_source_to_string(ChannelSource source) {
    switch (source) {
        case ChannelSource::real:
            return "real";
        case ChannelSource::simulated:
            return "simulated";
        default:
            return "unknown";
    }
}

const char *device_health_to_string(DeviceHealth health) {
    switch (health) {
        case DeviceHealth::ok:
            return "ok";
        case DeviceHealth::fault_active:
            return "fault_active";
        default:
            return "unknown";
    }
}

std::uint16_t fault_code_for_status(ChannelStatus status) {
    switch (status) {
        case ChannelStatus::not_found:
            return 0x0001U;
        case ChannelStatus::read_error:
            return 0x0002U;
        case ChannelStatus::range_error:
            return 0x0003U;
        case ChannelStatus::overtemp:
            return 0x0004U;
        case ChannelStatus::ok:
        default:
            return 0x0000U;
    }
}

const char *fault_code_name_for_status(ChannelStatus status) {
    switch (status) {
        case ChannelStatus::not_found:
            return "sensor_not_found";
        case ChannelStatus::read_error:
            return "sensor_read_error";
        case ChannelStatus::range_error:
            return "temperature_out_of_range";
        case ChannelStatus::overtemp:
            return "overtemp_warning";
        case ChannelStatus::ok:
        default:
            return "ok";
    }
}

const char *fault_message_for_status(ChannelStatus status) {
    switch (status) {
        case ChannelStatus::not_found:
            return "sensor did not respond on the one-wire bus";
        case ChannelStatus::read_error:
            return "sensor read failed or scratchpad CRC mismatch";
        case ChannelStatus::range_error:
            return "temperature outside configured valid range";
        case ChannelStatus::overtemp:
            return "temperature above configured warning threshold";
        case ChannelStatus::ok:
        default:
            return "no active fault";
    }
}

}  // namespace telemetry_node

namespace telemetry_node::health {

namespace {

constexpr std::size_t kPendingFaultCapacity = 8;

DeviceSnapshot g_latest_snapshot {};
std::array<ChannelStatus, kChannelCount> g_last_channel_status {};
FaultEvent g_pending_faults[kPendingFaultCapacity] {};
std::size_t g_pending_head = 0;
std::size_t g_pending_tail = 0;
std::size_t g_pending_count = 0;
bool g_has_processed_snapshot = false;
std::uint32_t g_last_processed_sample_index = 0;
portMUX_TYPE g_health_lock = portMUX_INITIALIZER_UNLOCKED;

float threshold_for_channel(const ChannelSample &channel) {
    const auto config = runtime_config::current();
    if (channel.source == ChannelSource::simulated) {
        return config.simulated_overtemp_threshold_c;
    }
    return config.real_overtemp_threshold_c;
}

ChannelStatus normalize_channel_status(const ChannelSample &channel) {
    if (channel.status != ChannelStatus::ok) {
        return channel.status;
    }

    if (channel.temperature_c > threshold_for_channel(channel)) {
        return ChannelStatus::overtemp;
    }

    return ChannelStatus::ok;
}

DeviceHealth derive_device_health(const DeviceSnapshot &snapshot) {
    for (std::size_t index = 0; index < snapshot.channel_count; ++index) {
        if (snapshot.channels[index].status != ChannelStatus::ok) {
            return DeviceHealth::fault_active;
        }
    }
    return DeviceHealth::ok;
}

void enqueue_fault_locked(const ChannelSample &channel, std::uint64_t uptime_ms) {
    FaultEvent event {};
    event.active = true;
    event.channel_id = channel.channel_id;
    event.source = channel.source;
    event.fault_code = fault_code_for_status(channel.status);
    std::snprintf(event.code, sizeof(event.code), "%s", fault_code_name_for_status(channel.status));
    std::snprintf(event.message, sizeof(event.message), "%s", fault_message_for_status(channel.status));
    event.temperature_c = channel.temperature_c;
    event.uptime_ms = uptime_ms;

    if (g_pending_count == kPendingFaultCapacity) {
        g_pending_head = (g_pending_head + 1U) % kPendingFaultCapacity;
        --g_pending_count;
    }

    g_pending_faults[g_pending_tail] = event;
    g_pending_tail = (g_pending_tail + 1U) % kPendingFaultCapacity;
    ++g_pending_count;
}

DeviceSnapshot make_initial_snapshot() {
    const auto config = runtime_config::current();
    DeviceSnapshot snapshot {};
    snapshot.sample_index = 0;
    snapshot.uptime_ms = app_config::uptime_ms();
    snapshot.channel_count = static_cast<std::uint8_t>(kChannelCount);
    snapshot.device_status = DeviceHealth::ok;

    ChannelSample real_channel {};
    real_channel.channel_id = 0;
    real_channel.source = ChannelSource::real;
    std::snprintf(real_channel.name, sizeof(real_channel.name), "%s", config.sensor_id);
    real_channel.status = ChannelStatus::not_found;

    ChannelSample simulated_channel {};
    simulated_channel.channel_id = 1;
    simulated_channel.source = ChannelSource::simulated;
    std::snprintf(simulated_channel.name, sizeof(simulated_channel.name), "%s", "simulated_hot_channel");
    simulated_channel.temperature_c = config.simulated_min_temp_c;
    simulated_channel.status = ChannelStatus::ok;

    snapshot.channels[0] = real_channel;
    snapshot.channels[1] = simulated_channel;
    return snapshot;
}

void process_snapshot(const DeviceSnapshot &raw_snapshot) {
    DeviceSnapshot diagnosed = raw_snapshot;

    for (std::size_t index = 0; index < diagnosed.channel_count; ++index) {
        diagnosed.channels[index].status = normalize_channel_status(diagnosed.channels[index]);
    }
    diagnosed.device_status = derive_device_health(diagnosed);

    taskENTER_CRITICAL(&g_health_lock);

    if (!g_has_processed_snapshot) {
        for (std::size_t index = 0; index < diagnosed.channel_count; ++index) {
            g_last_channel_status[index] = diagnosed.channels[index].status;
            if (diagnosed.channels[index].status != ChannelStatus::ok) {
                enqueue_fault_locked(diagnosed.channels[index], diagnosed.uptime_ms);
            }
        }
    } else {
        for (std::size_t index = 0; index < diagnosed.channel_count; ++index) {
            if (diagnosed.channels[index].status != g_last_channel_status[index]) {
                if (diagnosed.channels[index].status != ChannelStatus::ok) {
                    enqueue_fault_locked(diagnosed.channels[index], diagnosed.uptime_ms);
                }
                g_last_channel_status[index] = diagnosed.channels[index].status;
            }
        }
    }

    g_latest_snapshot = diagnosed;
    g_last_processed_sample_index = diagnosed.sample_index;
    g_has_processed_snapshot = true;

    taskEXIT_CRITICAL(&g_health_lock);
}

}  // namespace

void init() {
    taskENTER_CRITICAL(&g_health_lock);
    g_latest_snapshot = make_initial_snapshot();
    g_pending_head = 0;
    g_pending_tail = 0;
    g_pending_count = 0;
    g_has_processed_snapshot = false;
    g_last_processed_sample_index = 0;
    g_last_channel_status.fill(ChannelStatus::not_found);
    taskEXIT_CRITICAL(&g_health_lock);
}

void diagnostics_task(void *arg) {
    (void)arg;

    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        const auto raw_snapshot = sampler::get_latest_raw_snapshot();
        if (raw_snapshot.sample_index != 0 && raw_snapshot.sample_index != g_last_processed_sample_index) {
            process_snapshot(raw_snapshot);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
    }
}

DeviceSnapshot get_latest_snapshot() {
    taskENTER_CRITICAL(&g_health_lock);
    const auto snapshot = g_latest_snapshot;
    taskEXIT_CRITICAL(&g_health_lock);
    return snapshot;
}

bool take_pending_fault(FaultEvent &out_fault) {
    taskENTER_CRITICAL(&g_health_lock);
    if (g_pending_count == 0) {
        taskEXIT_CRITICAL(&g_health_lock);
        return false;
    }

    out_fault = g_pending_faults[g_pending_head];
    g_pending_head = (g_pending_head + 1U) % kPendingFaultCapacity;
    --g_pending_count;
    taskEXIT_CRITICAL(&g_health_lock);
    return true;
}

}  // namespace telemetry_node::health
