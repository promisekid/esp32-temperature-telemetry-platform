#pragma once

#include "project_types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace telemetry_platform::packet_codec {

class PacketDecoder {
  public:
    explicit PacketDecoder(common::ProtocolMode mode = common::ProtocolMode::auto_detect);

    void set_mode(common::ProtocolMode mode);
    common::ProtocolMode configured_mode() const;
    common::ProtocolMode active_mode() const;

    std::vector<common::DecodedPacket> push_bytes(const std::uint8_t *data, std::size_t size);

  private:
    std::vector<common::DecodedPacket> consume_json_lines();
    std::vector<common::DecodedPacket> consume_binary_frames();
    void update_active_mode();

    common::ProtocolMode configured_mode_ {common::ProtocolMode::auto_detect};
    common::ProtocolMode active_mode_ {common::ProtocolMode::auto_detect};
    std::vector<std::uint8_t> buffer_;
};

std::vector<std::uint8_t> encode_config_get(common::ProtocolMode mode);
std::vector<std::uint8_t> encode_config_set(const common::DeviceConfigProfile &profile, common::ProtocolMode mode);

}  // namespace telemetry_platform::packet_codec
