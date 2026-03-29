#pragma once

#include "project_types.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

namespace telemetry_platform::common {

struct ReplaySourceConfig {
    std::string path;
    std::uint32_t interval_ms {250};
    std::size_t binary_chunk_size {64};
    bool loop {true};
};

class ReplaySource {
  public:
    bool open(const ReplaySourceConfig &config, std::string *error_message = nullptr) {
        close();
        config_ = config;
        format_ = detect_format(config.path);
        stream_.open(config.path, std::ios::binary);
        if (!stream_.is_open()) {
            if (error_message != nullptr) {
                *error_message = "failed to open replay file: " + config.path;
            }
            return false;
        }
        next_emit_ms_ = 0;
        return true;
    }

    void close() {
        if (stream_.is_open()) {
            stream_.close();
        }
        next_emit_ms_ = 0;
    }

    [[nodiscard]] bool is_open() const {
        return stream_.is_open();
    }

    [[nodiscard]] ProtocolMode protocol_hint() const {
        return format_ == ReplayFormat::binary ? ProtocolMode::binary_v1 : ProtocolMode::auto_detect;
    }

    std::size_t read_some(
        std::uint8_t *buffer,
        std::size_t capacity,
        std::uint64_t now_ms,
        std::string *error_message = nullptr
    ) {
        if (!is_open() || buffer == nullptr || capacity == 0) {
            return 0;
        }

        if (next_emit_ms_ != 0 && now_ms < next_emit_ms_) {
            return 0;
        }

        if (format_ == ReplayFormat::jsonl) {
            return read_json_line(buffer, capacity, now_ms, error_message);
        }
        return read_binary_chunk(buffer, capacity, now_ms, error_message);
    }

  private:
    enum class ReplayFormat : std::uint8_t {
        jsonl = 0,
        binary = 1,
    };

    static ReplayFormat detect_format(const std::string &path) {
        const auto dot = path.find_last_of('.');
        if (dot == std::string::npos) {
            return ReplayFormat::jsonl;
        }

        std::string extension = path.substr(dot);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return extension == ".bin" ? ReplayFormat::binary : ReplayFormat::jsonl;
    }

    bool rewind_stream() {
        if (!config_.loop) {
            return false;
        }
        stream_.clear();
        stream_.seekg(0, std::ios::beg);
        return stream_.good();
    }

    std::size_t read_json_line(
        std::uint8_t *buffer,
        std::size_t capacity,
        std::uint64_t now_ms,
        std::string *error_message
    ) {
        std::string line;
        while (line.empty()) {
            if (!std::getline(stream_, line)) {
                if (!rewind_stream()) {
                    return 0;
                }
                continue;
            }
        }

        line.push_back('\n');
        if (line.size() > capacity) {
            if (error_message != nullptr) {
                *error_message = "replay line exceeds buffer capacity";
            }
            return 0;
        }

        std::memcpy(buffer, line.data(), line.size());
        next_emit_ms_ = now_ms + config_.interval_ms;
        return line.size();
    }

    std::size_t read_binary_chunk(
        std::uint8_t *buffer,
        std::size_t capacity,
        std::uint64_t now_ms,
        std::string *error_message
    ) {
        const auto bytes_to_read = std::min<std::size_t>(capacity, config_.binary_chunk_size);
        stream_.read(reinterpret_cast<char *>(buffer), static_cast<std::streamsize>(bytes_to_read));
        auto bytes_read = static_cast<std::size_t>(stream_.gcount());

        if (bytes_read == 0) {
            if (!rewind_stream()) {
                return 0;
            }
            stream_.read(reinterpret_cast<char *>(buffer), static_cast<std::streamsize>(bytes_to_read));
            bytes_read = static_cast<std::size_t>(stream_.gcount());
        }

        if (bytes_read == 0) {
            if (error_message != nullptr) {
                *error_message = "replay file does not contain readable data";
            }
            return 0;
        }

        next_emit_ms_ = now_ms + config_.interval_ms;
        return bytes_read;
    }

    ReplaySourceConfig config_ {};
    ReplayFormat format_ {ReplayFormat::jsonl};
    std::ifstream stream_;
    std::uint64_t next_emit_ms_ {0};
};

}  // namespace telemetry_platform::common
