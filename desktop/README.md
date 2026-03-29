# desktop

`desktop/` contains the PC-side modules for transport, decoding, state aggregation, and the Qt monitor MVP.

## Modules

- `common/`
  - shared data structures
  - protocol enums
  - replay input helpers
- `serial_link/`
  - Windows serial open/read/close
  - reconnect is handled by upper layers
- `packet_codec/`
  - `jsonl-v2` decoder
  - `binary-v1` decoder
  - auto mode detection
- `telemetry_service/`
  - latest device state
  - last `120 s` trend buffers
  - last `200` fault events
  - offline timeout after `12 s` without heartbeat
- `qt_monitor/`
  - Qt Widgets MVP
  - `Overview / Trend / Faults`

## Recommended dependency flow

```text
serial_link -> packet_codec -> telemetry_service -> qt_monitor
```

`qt_monitor` should not parse serial bytes directly. It only consumes the normalized service/state layer.

## Supported inputs

### Live serial

```powershell
C:\code\esp32-temperature-telemetry-platform\build-desktop\desktop\telemetry_service\telemetry_service.exe --port COM3 --baud 115200 --mode auto
```

```powershell
C:\code\esp32-temperature-telemetry-platform\build-desktop\desktop\qt_monitor\qt_monitor.exe --port COM3 --baud 115200 --mode auto
```

### Replay

```powershell
C:\code\esp32-temperature-telemetry-platform\build-desktop\desktop\telemetry_service\telemetry_service.exe --replay C:\code\esp32-temperature-telemetry-platform\data\samples\m2_binary_reference.bin --mode binary --duration 5
```

```powershell
C:\code\esp32-temperature-telemetry-platform\build-desktop\desktop\qt_monitor\qt_monitor.exe --replay C:\code\esp32-temperature-telemetry-platform\data\samples\m2_jsonl_v2_device_reference.jsonl --mode jsonl
```

## Recommended workflow

1. Validate `telemetry_service` with `--replay`.
2. Develop and verify `qt_monitor` with `--replay`.
3. Return to live serial only for end-to-end integration.

## Current priority

At this stage the goal is not advanced theming or config pages. The current priority is:

- stable dual-channel display
- protocol switching without UI breakage
- clear offline state
- reproducible replay-based demos
