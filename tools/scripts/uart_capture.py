#!/usr/bin/env python3
"""UART capture tool for JSONL v1/v2 and binary-v1 telemetry."""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from dataclasses import dataclass
from datetime import datetime
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


@dataclass
class CaptureOutputs:
    jsonl_path: Path
    csv_path: Path
    raw_path: Path | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Capture ESP32 temperature telemetry from UART.")
    parser.add_argument("--port", required=True, help="Serial port name, for example COM3.")
    parser.add_argument("--baud", type=int, default=115200, help="UART baud rate.")
    parser.add_argument(
        "--out-dir",
        default=str(Path("data") / "logs"),
        help="Directory for capture outputs.",
    )
    parser.add_argument(
        "--duration",
        type=int,
        default=0,
        help="Optional capture duration in seconds. 0 means run until interrupted.",
    )
    parser.add_argument(
        "--mode",
        choices=("auto", "jsonl", "binary"),
        default="auto",
        help="Protocol mode. Use auto to detect from incoming bytes.",
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


def open_csv_writer(csv_path: Path) -> tuple[Any, csv.DictWriter]:
    csv_file = csv_path.open("w", newline="", encoding="utf-8")
    writer = csv.DictWriter(
        csv_file,
        fieldnames=[
            "host_time",
            "device_uptime_ms",
            "sample_index",
            "channel_id",
            "source",
            "sensor_id_or_name",
            "temperature_c",
            "status",
        ],
    )
    writer.writeheader()
    return csv_file, writer


def normalized_rows(host_time: str, payload: dict[str, Any]) -> list[dict[str, Any]]:
    if payload.get("type") != "telemetry":
        return []

    if "channels" in payload:
        rows: list[dict[str, Any]] = []
        for channel in payload["channels"]:
            rows.append(
                {
                    "host_time": host_time,
                    "device_uptime_ms": payload.get("uptime_ms", 0),
                    "sample_index": payload.get("sample_index", 0),
                    "channel_id": channel.get("channel_id", 0),
                    "source": channel.get("source", "real"),
                    "sensor_id_or_name": channel.get("name", f"channel_{channel.get('channel_id', 0)}"),
                    "temperature_c": channel.get("temperature_c", 0.0),
                    "status": channel.get("status", "unknown"),
                }
            )
        return rows

    return [
        {
            "host_time": host_time,
            "device_uptime_ms": payload.get("uptime_ms", 0),
            "sample_index": payload.get("sample_index", 0),
            "channel_id": 0,
            "source": "real",
            "sensor_id_or_name": payload.get("sensor_id", "ds18b20_0"),
            "temperature_c": payload.get("temperature_c", 0.0),
            "status": payload.get("status", "unknown"),
        }
    ]


def format_console_line(host_time: str, payload: dict[str, Any]) -> str:
    msg_type = payload.get("type")
    if msg_type == "heartbeat":
        return (
            f"[{host_time}] HEARTBEAT protocol={payload.get('protocol', PROTOCOL_JSONL)} "
            f"uptime={payload.get('uptime_ms')} fw={payload.get('fw_version', '-')}"
        )
    if msg_type == "telemetry":
        if "channels" in payload:
            channel_desc = ", ".join(
                f"ch{item.get('channel_id')}={item.get('temperature_c')}C/{item.get('status')}"
                for item in payload["channels"]
            )
            return (
                f"[{host_time}] TELEMETRY protocol={payload.get('protocol', PROTOCOL_JSONL)} "
                f"sample={payload.get('sample_index', 0)} {channel_desc}"
            )
        return (
            f"[{host_time}] TELEMETRY sensor={payload.get('sensor_id')} "
            f"temp={payload.get('temperature_c')}C status={payload.get('status')} "
            f"sample={payload.get('sample_index', 0)}"
        )
    if msg_type == "fault":
        return (
            f"[{host_time}] FAULT protocol={payload.get('protocol', PROTOCOL_JSONL)} "
            f"channel={payload.get('channel_id', 0)} code={payload.get('code')} "
            f"message={payload.get('message')}"
        )
    return f"[{host_time}] RAW {payload}"


def detect_mode(mode: str, buffer: bytearray) -> str | None:
    if mode != "auto":
        return mode

    stripped = bytes(buffer).lstrip(b" \r\n\t")
    if not stripped:
        return None
    if stripped.startswith(MAGIC):
        return "binary"
    if stripped[:1] == b"{" or chr(stripped[0]).isprintable():
        return "jsonl"
    return None


def parse_json_line(line: str) -> dict[str, Any] | None:
    stripped = line.strip()
    if not stripped:
        return None
    if not stripped.startswith("{"):
        print(f"[device-log] {stripped}")
        return None

    payload = json.loads(stripped)
    payload_type = payload.get("type")
    if payload_type == "heartbeat":
        payload.setdefault("protocol", PROTOCOL_JSONL)
        payload.setdefault("channel_count", 1)
        payload.setdefault("device_status", "ok")
        return payload
    if payload_type == "telemetry":
        payload.setdefault("protocol", PROTOCOL_JSONL)
        return payload
    if payload_type == "fault":
        payload.setdefault("protocol", PROTOCOL_JSONL)
        payload.setdefault("channel_id", 0)
        payload.setdefault("source", "real")
        return payload
    raise ValueError(f"unsupported message type: {payload_type!r}")


class JsonlDecoder:
    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[dict[str, Any]]:
        self._buffer.extend(data)
        messages: list[dict[str, Any]] = []
        while True:
            try:
                newline = self._buffer.index(0x0A)
            except ValueError:
                break

            line = self._buffer[:newline].decode("utf-8", errors="replace")
            del self._buffer[: newline + 1]

            try:
                payload = parse_json_line(line)
            except (json.JSONDecodeError, ValueError) as exc:
                now = datetime.now().isoformat(timespec="seconds")
                print(f"[{now}] INVALID {line.strip()} ({exc})", file=sys.stderr)
                continue

            if payload is not None:
                messages.append(payload)
        return messages


class BinaryDecoder:
    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[dict[str, Any]]:
        self._buffer.extend(data)
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
        version = frame[2]
        msg_type = frame[3]
        sequence = int.from_bytes(frame[4:6], "little")
        payload_length = int.from_bytes(frame[6:8], "little")
        payload = frame[8 : 8 + payload_length]

        if version != 0x01:
            return None

        if msg_type == 0x01:
            uptime_ms = int.from_bytes(payload[0:4], "little")
            channel_count = payload[4]
            channels = []
            offset = 5
            for _ in range(channel_count):
                channel_id = payload[offset]
                source = "real" if payload[offset + 1] == 0 else "simulated"
                status_map = {
                    0: "ok",
                    1: "not_found",
                    2: "read_error",
                    3: "range_error",
                    4: "overtemp",
                }
                status = status_map.get(payload[offset + 2], "unknown")
                temperature_c = int.from_bytes(payload[offset + 3 : offset + 5], "little", signed=True) / 100.0
                channels.append(
                    {
                        "channel_id": channel_id,
                        "source": source,
                        "name": "ds18b20_0" if source == "real" else "simulated_hot_channel",
                        "temperature_c": temperature_c,
                        "status": status,
                    }
                )
                offset += 5

            return {
                "type": "telemetry",
                "protocol": PROTOCOL_BINARY,
                "sample_index": sequence,
                "uptime_ms": uptime_ms,
                "channels": channels,
            }

        if msg_type == 0x02:
            return {
                "type": "heartbeat",
                "protocol": PROTOCOL_BINARY,
                "uptime_ms": int.from_bytes(payload[0:4], "little"),
                "fw_version": "",
                "channel_count": payload[4],
                "device_status": "ok" if payload[5] == 0 else "fault_active",
            }

        if msg_type == 0x03:
            fault_code = int.from_bytes(payload[2:4], "little")
            code_map = {
                0x0001: "sensor_not_found",
                0x0002: "sensor_read_error",
                0x0003: "temperature_out_of_range",
                0x0004: "overtemp_warning",
            }
            return {
                "type": "fault",
                "protocol": PROTOCOL_BINARY,
                "channel_id": payload[0],
                "source": "real" if payload[1] == 0 else "simulated",
                "code": code_map.get(fault_code, "unknown_fault"),
                "message": code_map.get(fault_code, "unknown_fault"),
                "temperature_c": int.from_bytes(payload[4:6], "little", signed=True) / 100.0,
                "uptime_ms": int.from_bytes(payload[6:10], "little"),
            }

        return None


def main() -> int:
    args = parse_args()
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    outputs = CaptureOutputs(
        jsonl_path=out_dir / f"telemetry_capture_{timestamp}.jsonl",
        csv_path=out_dir / f"telemetry_capture_{timestamp}.csv",
        raw_path=out_dir / f"telemetry_capture_{timestamp}.bin" if args.mode in ("auto", "binary") else None,
    )

    try:
        serial_port = serial.Serial(args.port, args.baud, timeout=0.2)
    except SerialException as exc:
        print(f"Failed to open serial port {args.port}: {exc}", file=sys.stderr)
        return 2

    start_time = time.time()
    detected_mode: str | None = None
    detection_buffer = bytearray()
    decoder: JsonlDecoder | BinaryDecoder | None = None

    print(f"Capturing telemetry on {args.port} @ {args.baud} baud")
    print(f"JSONL output: {outputs.jsonl_path}")
    print(f"CSV output:   {outputs.csv_path}")
    if outputs.raw_path is not None:
        print(f"Binary output: {outputs.raw_path}")

    with serial_port, outputs.jsonl_path.open("w", encoding="utf-8") as jsonl_file:
        csv_file, csv_writer = open_csv_writer(outputs.csv_path)
        with csv_file:
            raw_file = None
            try:
                while True:
                    if args.duration and (time.time() - start_time) >= args.duration:
                        print("Capture duration reached, stopping.")
                        break

                    try:
                        chunk = serial_port.read(256)
                    except SerialException as exc:
                        print(f"Serial read failed: {exc}", file=sys.stderr)
                        return 3

                    if not chunk:
                        continue

                    feed_data = chunk
                    if detected_mode is None:
                        detection_buffer.extend(chunk)
                        detected_mode = detect_mode(args.mode, detection_buffer)
                        if detected_mode is None:
                            continue
                        print(f"Detected protocol mode: {detected_mode}")
                        decoder = JsonlDecoder() if detected_mode == "jsonl" else BinaryDecoder()
                        feed_data = bytes(detection_buffer)
                        detection_buffer.clear()

                    if raw_file is not None and detected_mode == "binary":
                        raw_file.write(feed_data)
                        raw_file.flush()
                    elif detected_mode == "binary" and outputs.raw_path is not None:
                        raw_file = outputs.raw_path.open("wb")
                        raw_file.write(feed_data)
                        raw_file.flush()

                    assert decoder is not None
                    for payload in decoder.feed(feed_data):
                        host_time = datetime.now().isoformat(timespec="seconds")
                        print(format_console_line(host_time, payload))
                        jsonl_file.write(json.dumps(payload, ensure_ascii=False) + "\n")
                        jsonl_file.flush()
                        for row in normalized_rows(host_time, payload):
                            csv_writer.writerow(row)
                            csv_file.flush()
            finally:
                if raw_file is not None:
                    raw_file.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
