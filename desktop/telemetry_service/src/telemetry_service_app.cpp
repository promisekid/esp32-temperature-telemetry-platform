#include "telemetry_service_app.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace telemetry_platform::telemetry_service {

namespace {

std::uint64_t now_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

common::ProtocolMode parse_mode(const std::string &value) {
    return common::protocol_mode_from_string(value).value_or(common::ProtocolMode::auto_detect);
}

void print_usage() {
    std::cout << "usage: telemetry_service [--port COM3 | --replay PATH] "
                 "[--baud 115200] [--mode auto|jsonl|binary] "
                 "[--duration 30] [--replay-interval 250] [--quiet] [--debug-io]\n";
}

void print_snapshot_summary(const common::DeviceState &state) {
    std::cout << "[service] online=" << (state.online ? "true" : "false")
              << " fw=" << (state.firmware_version.empty() ? "-" : state.firmware_version)
              << " device_status=" << common::to_string(state.latest_snapshot.device_status)
              << " channels=" << static_cast<unsigned>(state.latest_snapshot.channel_count)
              << "\n";

    for (const auto &channel : state.latest_snapshot.channels) {
        std::cout << "  channel_" << static_cast<unsigned>(channel.channel_id)
                  << " source=" << common::to_string(channel.source)
                  << " temp=" << channel.temperature_c
                  << " status=" << common::to_string(channel.status)
                  << " name=" << channel.name
                  << "\n";
    }

    if (!state.recent_faults.empty()) {
        const auto &fault = state.recent_faults.front();
        std::cout << "  latest_fault channel=" << static_cast<unsigned>(fault.channel_id)
                  << " code=" << fault.code
                  << " message=" << fault.message
                  << "\n";
    }
}

}  // namespace

int TelemetryServiceApp::run(int argc, char **argv) {
    TelemetryServiceAppConfig config {};

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--port" && (index + 1) < argc) {
            config.port_name = argv[++index];
        } else if (arg == "--replay" && (index + 1) < argc) {
            config.replay_path = argv[++index];
        } else if (arg == "--baud" && (index + 1) < argc) {
            config.baud_rate = static_cast<std::uint32_t>(std::strtoul(argv[++index], nullptr, 10));
        } else if (arg == "--mode" && (index + 1) < argc) {
            config.mode = parse_mode(argv[++index]);
        } else if (arg == "--duration" && (index + 1) < argc) {
            config.duration_seconds = static_cast<std::uint32_t>(std::strtoul(argv[++index], nullptr, 10));
        } else if (arg == "--replay-interval" && (index + 1) < argc) {
            config.replay_interval_ms = static_cast<std::uint32_t>(std::strtoul(argv[++index], nullptr, 10));
        } else if (arg == "--quiet") {
            config.quiet = true;
        } else if (arg == "--debug-io") {
            config.debug_io = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            print_usage();
            return 2;
        }
    }

    if (config.port_name.empty() && config.replay_path.empty()) {
        print_usage();
        return 2;
    }

    return run_with_config(config);
}

int TelemetryServiceApp::run_with_config(const TelemetryServiceAppConfig &config) {
    serial_link::SerialPort port;
    common::ReplaySource replay_source;
    serial_link::SerialPortConfig port_config {};
    port_config.port_name = config.port_name;
    port_config.baud_rate = config.baud_rate;
    port_config.read_timeout_ms = 100;

    packet_codec::PacketDecoder decoder(config.mode);
    TelemetryStore store;

    const auto started_at = now_ms();
    std::uint64_t last_summary_ms = 0;
    // jsonl-v2 telemetry lines are much larger than the original M1 single-channel payloads.
    std::array<std::uint8_t, 2048> read_buffer {};

    if (!config.replay_path.empty()) {
        common::ReplaySourceConfig replay_config {};
        replay_config.path = config.replay_path;
        replay_config.interval_ms = config.replay_interval_ms;
        std::string replay_error;
        if (!replay_source.open(replay_config, &replay_error)) {
            std::cerr << "[service] " << replay_error << "\n";
            return 2;
        }
        if (config.mode == common::ProtocolMode::auto_detect) {
            decoder.set_mode(replay_source.protocol_hint());
        }
        if (config.debug_io) {
            std::cout << "[service] replay opened: " << config.replay_path
                      << " interval_ms=" << config.replay_interval_ms
                      << " mode=" << common::to_string(decoder.configured_mode())
                      << "\n";
        }
    }

    while (true) {
        const auto current_ms = now_ms();
        if (config.duration_seconds > 0 && (current_ms - started_at) >= (config.duration_seconds * 1000ULL)) {
            break;
        }

        if (!config.replay_path.empty()) {
            std::string replay_error;
            const auto bytes_read = replay_source.read_some(read_buffer.data(), read_buffer.size(), current_ms, &replay_error);
            if (!replay_error.empty()) {
                std::cerr << "[service] " << replay_error << "\n";
                return 3;
            }

            if (bytes_read > 0) {
                auto packets = decoder.push_bytes(read_buffer.data(), bytes_read);
                if (config.debug_io) {
                    std::cout << "[service] replay read " << bytes_read << " bytes, decoded " << packets.size()
                              << " packets, active_mode=" << common::to_string(decoder.active_mode()) << "\n";
                }
                for (auto &packet : packets) {
                    store.apply(packet, current_ms);
                }
            }

            store.evaluate_timeouts(current_ms);
        } else {
            if (!port.is_open()) {
                std::string error_message;
                if (!port.open(port_config, &error_message)) {
                    std::cerr << "[service] " << error_message << "\n";
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                if (config.debug_io) {
                    std::cout << "[service] serial port opened: " << config.port_name
                              << " baud=" << config.baud_rate
                              << " mode=" << common::to_string(config.mode)
                              << "\n";
                }
            }

            std::string error_message;
            const auto bytes_read = port.read_some(read_buffer.data(), read_buffer.size(), &error_message);
            if (!error_message.empty()) {
                std::cerr << "[service] " << error_message << "\n";
                port.close();
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            if (bytes_read > 0) {
                auto packets = decoder.push_bytes(read_buffer.data(), bytes_read);
                if (config.debug_io) {
                    std::cout << "[service] read " << bytes_read << " bytes, decoded " << packets.size()
                              << " packets, active_mode=" << common::to_string(decoder.active_mode()) << "\n";
                }
                for (auto &packet : packets) {
                    store.apply(packet, current_ms);
                }
            }

            store.evaluate_timeouts(current_ms);
        }

        if (!config.quiet && (current_ms - last_summary_ms) >= 1000ULL) {
            print_snapshot_summary(store.snapshot());
            last_summary_ms = current_ms;
        }
    }

    return 0;
}

}  // namespace telemetry_platform::telemetry_service
