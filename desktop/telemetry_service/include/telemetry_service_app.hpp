#pragma once

#include "packet_codec.hpp"
#include "replay_source.hpp"
#include "serial_link.hpp"
#include "telemetry_store.hpp"

#include <cstdint>
#include <string>

namespace telemetry_platform::telemetry_service {

struct TelemetryServiceAppConfig {
    std::string port_name;
    std::string replay_path;
    std::uint32_t baud_rate {115200};
    common::ProtocolMode mode {common::ProtocolMode::auto_detect};
    std::uint32_t duration_seconds {0};
    std::uint32_t replay_interval_ms {250};
    bool quiet {false};
    bool debug_io {false};
};

class TelemetryServiceApp {
  public:
    int run(int argc, char **argv);

  private:
    int run_with_config(const TelemetryServiceAppConfig &config);
};

}  // namespace telemetry_platform::telemetry_service
