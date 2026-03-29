#!/usr/bin/env python3
"""Serial tool for ESP32 runtime config get/set."""

from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

try:
    import serial
    from serial import SerialException
except ImportError as exc:  # pragma: no cover - runtime guard
    raise SystemExit(
        "pyserial is required. Install it with: python -m pip install pyserial"
    ) from exc


PROTOCOL_JSONL = "jsonl-v2"
PROTOCOL_BINARY = "binary-v1"
MAGIC = b"\xAA\x55"
HEADER_SIZE = 8
MIN_FRAME_SIZE = 10
CONFIG_GET_TYPE = 0x20
CONFIG_SET_TYPE = 0x21
CONFIG_REPORT_TYPE = 0x10
COMMAND_ACK_TYPE = 0x11
CONFIG_TEXT_BYTES = 24


@dataclass
class DeviceConfigProfile:
    preferred_protocol: str = PROTOCOL_BINARY
    sensor_gpio: int = 4
    sample_period_ms: int = 1000
    heartbeat_period_ms: int = 5000
    sensor_id: str = "ds18b20_0"
    fw_version: str = "m2-dev"
    valid_min_temp_c: float = -55.0
    valid_max_temp_c: float = 125.0
    real_overtemp_threshold_c: float = 30.0
    simulated_min_temp_c: float = 34.0
    simulated_max_temp_c: float = 38.0
    simulated_step_c: float = 0.4
    simulated_overtemp_threshold_c: float = 37.0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Query or update ESP32 runtime configuration.")
    parser.add_argument("--port", default="COM3", help="Serial port name.")
    parser.add_argument("--baud", type=int, default=115200, help="UART baud rate.")
    parser.add_argument("--timeout", type=float, default=3.0, help="Response timeout in seconds.")
    parser.add_argument(
        "--mode",
        choices=("jsonl", "binary"),
        default="binary",
        help="Command encoding format.",
    )
    parser.add_argument(
        "--response-mode",
        choices=("auto", "jsonl", "binary"),
        default="auto",
        help="Response decoding format. Use auto to survive protocol switches.",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("get", help="Request current device config.")

    set_parser = subparsers.add_parser("set", help="Push a config profile to device.")
    set_parser.add_argument("--preferred-protocol", choices=("jsonl-v2", "binary-v1"), default="binary-v1")
    set_parser.add_argument("--sensor-gpio", type=int, default=4)
    set_parser.add_argument("--sample-period-ms", type=int, default=1000)
    set_parser.add_argument("--heartbeat-period-ms", type=int, default=5000)
    set_parser.add_argument("--sensor-id", default="ds18b20_0")
    set_parser.add_argument("--fw-version", default="m2-dev")
    set_parser.add_argument("--valid-min-temp-c", type=float, default=-55.0)
    set_parser.add_argument("--valid-max-temp-c", type=float, default=125.0)
    set_parser.add_argument("--real-overtemp-threshold-c", type=float, default=30.0)
    set_parser.add_argument("--simulated-min-temp-c", type=float, default=34.0)
    set_parser.add_argument("--simulated-max-temp-c", type=float, default=38.0)
    set_parser.add_argument("--simulated-step-c", type=float, default=0.4)
    set_parser.add_argument("--simulated-overtemp-threshold-c", type=float, default=37.0)
    set_parser.add_argument(
        "--profile-json",
        help="Optional JSON file containing a full config profile. CLI flags override file values.",
    )

    return parser.parse_args()


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def append_u8(buffer: bytearray, value: int) -> None:
    buffer.append(value & 0xFF)


def append_u16_le(buffer: bytearray, value: int) -> None:
    buffer.extend(int(value).to_bytes(2, "little", signed=False))


def append_i16_le(buffer: bytearray, value: int) -> None:
    buffer.extend(int(value).to_bytes(2, "little", signed=True))


def append_fixed_string(buffer: bytearray, value: str, size: int) -> None:
    encoded = value.encode("utf-8")[: size - 1]
    buffer.extend(encoded)
    buffer.extend(b"\x00" * (size - len(encoded)))


def encode_binary_frame(message_type: int, payload: bytes, sequence: int = 0) -> bytes:
    frame = bytearray()
    append_u8(frame, MAGIC[0])
    append_u8(frame, MAGIC[1])
    append_u8(frame, 0x01)
    append_u8(frame, message_type)
    append_u16_le(frame, sequence)
    append_u16_le(frame, len(payload))
    frame.extend(payload)
    append_u16_le(frame, crc16_ccitt_false(bytes(frame[2:])))
    return bytes(frame)


def encode_json_command(command: str, profile: DeviceConfigProfile | None = None) -> bytes:
    payload: dict[str, Any] = {"type": command}
    if profile is not None:
        payload.update(
            {
                "preferred_protocol": profile.preferred_protocol,
                "sensor_gpio": profile.sensor_gpio,
                "sample_period_ms": profile.sample_period_ms,
                "heartbeat_period_ms": profile.heartbeat_period_ms,
                "sensor_id": profile.sensor_id,
                "fw_version": profile.fw_version,
                "valid_min_temp_c": profile.valid_min_temp_c,
                "valid_max_temp_c": profile.valid_max_temp_c,
                "real_overtemp_threshold_c": profile.real_overtemp_threshold_c,
                "simulated_min_temp_c": profile.simulated_min_temp_c,
                "simulated_max_temp_c": profile.simulated_max_temp_c,
                "simulated_step_c": profile.simulated_step_c,
                "simulated_overtemp_threshold_c": profile.simulated_overtemp_threshold_c,
            }
        )
    return (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")


def encode_binary_config_get() -> bytes:
    return encode_binary_frame(CONFIG_GET_TYPE, b"")


def encode_binary_config_set(profile: DeviceConfigProfile) -> bytes:
    payload = bytearray()
    append_u8(payload, 1 if profile.preferred_protocol == PROTOCOL_BINARY else 0)
    append_u8(payload, profile.sensor_gpio)
    append_u16_le(payload, profile.sample_period_ms)
    append_u16_le(payload, profile.heartbeat_period_ms)
    append_i16_le(payload, round(profile.valid_min_temp_c * 10))
    append_i16_le(payload, round(profile.valid_max_temp_c * 10))
    append_i16_le(payload, round(profile.real_overtemp_threshold_c * 10))
    append_i16_le(payload, round(profile.simulated_min_temp_c * 10))
    append_i16_le(payload, round(profile.simulated_max_temp_c * 10))
    append_i16_le(payload, round(profile.simulated_step_c * 100))
    append_i16_le(payload, round(profile.simulated_overtemp_threshold_c * 10))
    append_fixed_string(payload, profile.sensor_id, CONFIG_TEXT_BYTES)
    append_fixed_string(payload, profile.fw_version, CONFIG_TEXT_BYTES)
    return encode_binary_frame(CONFIG_SET_TYPE, bytes(payload))


def read_u16_le(data: bytes) -> int:
    return int.from_bytes(data[:2], "little")


def read_i16_le(data: bytes) -> int:
    return int.from_bytes(data[:2], "little", signed=True)


def trim_c_string(data: bytes) -> str:
    return data.split(b"\x00", 1)[0].decode("utf-8", errors="ignore")


class JsonlResponseDecoder:
    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, chunk: bytes) -> list[dict[str, Any]]:
        self._buffer.extend(chunk)
        messages: list[dict[str, Any]] = []
        while True:
            try:
                newline = self._buffer.index(0x0A)
            except ValueError:
                break
            line = self._buffer[:newline].decode("utf-8", errors="replace").strip()
            del self._buffer[: newline + 1]
            if not line or not line.startswith("{"):
                continue
            try:
                payload = json.loads(line)
            except json.JSONDecodeError:
                continue
            if payload.get("type") in {"config", "command_ack"}:
                messages.append(payload)
        return messages


class BinaryResponseDecoder:
    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, chunk: bytes) -> list[dict[str, Any]]:
        self._buffer.extend(chunk)
        messages: list[dict[str, Any]] = []

        while len(self._buffer) >= MIN_FRAME_SIZE:
            magic_index = self._buffer.find(MAGIC)
            if magic_index < 0:
                self._buffer.clear()
                break
            if magic_index > 0:
                del self._buffer[:magic_index]
            if len(self._buffer) < MIN_FRAME_SIZE:
                break

            payload_length = int.from_bytes(self._buffer[6:8], "little")
            frame_length = HEADER_SIZE + payload_length + 2
            if len(self._buffer) < frame_length:
                break

            frame = bytes(self._buffer[:frame_length])
            expected_crc = int.from_bytes(frame[-2:], "little")
            actual_crc = crc16_ccitt_false(frame[2:-2])
            if expected_crc != actual_crc:
                del self._buffer[0]
                continue

            del self._buffer[:frame_length]
            decoded = self._decode_frame(frame)
            if decoded is not None:
                messages.append(decoded)

        return messages

    def _decode_frame(self, frame: bytes) -> dict[str, Any] | None:
        payload_length = int.from_bytes(frame[6:8], "little")
        payload = frame[8 : 8 + payload_length]
        msg_type = frame[3]

        if msg_type == CONFIG_REPORT_TYPE and payload_length >= 69:
            return {
                "type": "config",
                "protocol": PROTOCOL_BINARY,
                "preferred_protocol": PROTOCOL_BINARY if payload[0] == 1 else PROTOCOL_JSONL,
                "sensor_gpio": payload[1],
                "sample_period_ms": read_u16_le(payload[2:4]),
                "heartbeat_period_ms": read_u16_le(payload[4:6]),
                "valid_min_temp_c": read_i16_le(payload[6:8]) / 10.0,
                "valid_max_temp_c": read_i16_le(payload[8:10]) / 10.0,
                "real_overtemp_threshold_c": read_i16_le(payload[10:12]) / 10.0,
                "simulated_min_temp_c": read_i16_le(payload[12:14]) / 10.0,
                "simulated_max_temp_c": read_i16_le(payload[14:16]) / 10.0,
                "simulated_step_c": read_i16_le(payload[16:18]) / 100.0,
                "simulated_overtemp_threshold_c": read_i16_le(payload[18:20]) / 10.0,
                "sensor_id": trim_c_string(payload[20:44]),
                "fw_version": trim_c_string(payload[44:68]),
                "persisted": bool(payload[68]),
            }

        if msg_type == COMMAND_ACK_TYPE and payload_length >= 50:
            command_map = {
                CONFIG_GET_TYPE: "config_get",
                CONFIG_SET_TYPE: "config_set",
            }
            return {
                "type": "command_ack",
                "protocol": PROTOCOL_BINARY,
                "success": bool(payload[0]),
                "command": command_map.get(payload[1], f"0x{payload[1]:02x}"),
                "message": trim_c_string(payload[2:50]),
            }

        return None


class AutoResponseDecoder:
    def __init__(self) -> None:
        self._json_decoder = JsonlResponseDecoder()
        self._binary_decoder = BinaryResponseDecoder()

    def feed(self, chunk: bytes) -> list[dict[str, Any]]:
        messages: list[dict[str, Any]] = []
        messages.extend(self._json_decoder.feed(chunk))
        messages.extend(self._binary_decoder.feed(chunk))
        return messages


def build_profile(args: argparse.Namespace) -> DeviceConfigProfile:
    profile = DeviceConfigProfile()
    if args.profile_json:
        payload = json.loads(Path(args.profile_json).read_text(encoding="utf-8"))
        for key, value in payload.items():
            if hasattr(profile, key):
                setattr(profile, key, value)

    overrides = {
        "preferred_protocol": args.preferred_protocol,
        "sensor_gpio": args.sensor_gpio,
        "sample_period_ms": args.sample_period_ms,
        "heartbeat_period_ms": args.heartbeat_period_ms,
        "sensor_id": args.sensor_id,
        "fw_version": args.fw_version,
        "valid_min_temp_c": args.valid_min_temp_c,
        "valid_max_temp_c": args.valid_max_temp_c,
        "real_overtemp_threshold_c": args.real_overtemp_threshold_c,
        "simulated_min_temp_c": args.simulated_min_temp_c,
        "simulated_max_temp_c": args.simulated_max_temp_c,
        "simulated_step_c": args.simulated_step_c,
        "simulated_overtemp_threshold_c": args.simulated_overtemp_threshold_c,
    }
    for key, value in overrides.items():
        setattr(profile, key, value)
    return profile


def send_command(port: serial.Serial, args: argparse.Namespace) -> None:
    port.reset_input_buffer()

    if args.command == "get":
        payload = encode_json_command("config_get") if args.mode == "jsonl" else encode_binary_config_get()
    else:
        profile = build_profile(args)
        payload = encode_json_command("config_set", profile) if args.mode == "jsonl" else encode_binary_config_set(profile)

    port.write(payload)
    port.flush()


def wait_for_response(port: serial.Serial, response_mode: str, timeout_s: float) -> list[dict[str, Any]]:
    if response_mode == "jsonl":
        decoder: JsonlResponseDecoder | BinaryResponseDecoder | AutoResponseDecoder = JsonlResponseDecoder()
    elif response_mode == "binary":
        decoder = BinaryResponseDecoder()
    else:
        decoder = AutoResponseDecoder()
    responses: list[dict[str, Any]] = []
    deadline = time.time() + timeout_s

    while time.time() < deadline:
        try:
            chunk = port.read(256)
        except SerialException as exc:
            raise SystemExit(f"Serial read failed: {exc}") from exc

        if not chunk:
            continue

        for payload in decoder.feed(chunk):
            responses.append(payload)
            if payload.get("type") == "config" and (
                any(item.get("type") == "command_ack" for item in responses) or len(responses) >= 1
            ):
                return responses

    return responses


def main() -> int:
    args = parse_args()

    try:
        port = serial.Serial(args.port, args.baud, timeout=0.2)
    except SerialException as exc:
        print(f"Failed to open serial port {args.port}: {exc}", file=sys.stderr)
        return 2

    with port:
        send_command(port, args)
        responses = wait_for_response(port, args.response_mode, args.timeout)

    if not responses:
        print("No config response received before timeout.", file=sys.stderr)
        return 3

    print(json.dumps(responses, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
