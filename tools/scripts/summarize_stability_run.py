#!/usr/bin/env python3
"""Summarize a stability capture run directory into markdown and JSON metrics."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Any


@dataclass
class ChannelMetrics:
    channel_id: int
    source: str
    samples: int
    min_temp_c: float | None
    max_temp_c: float | None
    statuses: dict[str, int]


@dataclass
class RunMetrics:
    run_dir: str
    jsonl_path: str | None
    csv_path: str | None
    bin_path: str | None
    telemetry_rows: int
    unique_samples: int
    heartbeat_count: int
    fault_count: int
    first_host_time: str | None
    last_host_time: str | None
    approx_duration_seconds: float | None
    channels: list[ChannelMetrics]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Summarize a telemetry stability run.")
    parser.add_argument("--run-dir", required=True, help="Path to a run_<timestamp> directory.")
    return parser.parse_args()


def parse_iso_timestamp(value: str | None) -> datetime | None:
    if not value:
        return None
    try:
        return datetime.fromisoformat(value)
    except ValueError:
        return None


def detect_latest_file(run_dir: Path, pattern: str) -> Path | None:
    matches = sorted(run_dir.glob(pattern), key=lambda item: item.stat().st_mtime, reverse=True)
    return matches[0] if matches else None


def summarize_run(run_dir: Path) -> RunMetrics:
    jsonl_path = detect_latest_file(run_dir, "*.jsonl")
    csv_path = detect_latest_file(run_dir, "*.csv")
    bin_path = detect_latest_file(run_dir, "*.bin")

    heartbeat_count = 0
    fault_count = 0
    if jsonl_path and jsonl_path.exists():
        for line in jsonl_path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line.startswith("{"):
                continue
            try:
                payload = json.loads(line)
            except json.JSONDecodeError:
                continue
            payload_type = payload.get("type")
            if payload_type == "heartbeat":
                heartbeat_count += 1
            elif payload_type == "fault":
                fault_count += 1

    telemetry_rows = 0
    unique_samples: set[str] = set()
    first_host_time: str | None = None
    last_host_time: str | None = None
    per_channel_temps: dict[int, list[float]] = defaultdict(list)
    per_channel_source: dict[int, str] = {}
    per_channel_status: dict[int, Counter[str]] = defaultdict(Counter)

    if csv_path and csv_path.exists():
        with csv_path.open("r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle)
            for row in reader:
                telemetry_rows += 1
                sample_index = row.get("sample_index", "")
                channel_id = row.get("channel_id", "")
                if sample_index:
                    unique_samples.add(sample_index)
                host_time = row.get("host_time")
                if first_host_time is None:
                    first_host_time = host_time
                last_host_time = host_time

                try:
                    channel_int = int(channel_id)
                except (TypeError, ValueError):
                    continue

                source = row.get("source", "unknown")
                status = row.get("status", "unknown")
                per_channel_source[channel_int] = source
                per_channel_status[channel_int][status] += 1

                try:
                    per_channel_temps[channel_int].append(float(row.get("temperature_c", "0")))
                except ValueError:
                    continue

    first_dt = parse_iso_timestamp(first_host_time)
    last_dt = parse_iso_timestamp(last_host_time)
    duration_seconds: float | None = None
    if first_dt and last_dt:
        duration_seconds = max((last_dt - first_dt).total_seconds(), 0.0)

    channel_metrics: list[ChannelMetrics] = []
    for channel_id in sorted(per_channel_source):
        temps = per_channel_temps.get(channel_id, [])
        channel_metrics.append(
            ChannelMetrics(
                channel_id=channel_id,
                source=per_channel_source.get(channel_id, "unknown"),
                samples=len(temps),
                min_temp_c=min(temps) if temps else None,
                max_temp_c=max(temps) if temps else None,
                statuses=dict(per_channel_status.get(channel_id, Counter())),
            )
        )

    return RunMetrics(
        run_dir=str(run_dir),
        jsonl_path=str(jsonl_path) if jsonl_path else None,
        csv_path=str(csv_path) if csv_path else None,
        bin_path=str(bin_path) if bin_path else None,
        telemetry_rows=telemetry_rows,
        unique_samples=len(unique_samples),
        heartbeat_count=heartbeat_count,
        fault_count=fault_count,
        first_host_time=first_host_time,
        last_host_time=last_host_time,
        approx_duration_seconds=duration_seconds,
        channels=channel_metrics,
    )


def render_markdown(metrics: RunMetrics) -> str:
    lines = [
        "# Stability Capture Summary",
        "",
        f"- Run directory: {metrics.run_dir}",
        f"- JSONL: {metrics.jsonl_path or 'not generated'}",
        f"- CSV: {metrics.csv_path or 'not generated'}",
        f"- Binary: {metrics.bin_path or 'not generated'}",
        f"- Telemetry rows: {metrics.telemetry_rows}",
        f"- Unique sample_index count: {metrics.unique_samples}",
        f"- Heartbeat count: {metrics.heartbeat_count}",
        f"- Fault count: {metrics.fault_count}",
        f"- First host_time: {metrics.first_host_time or 'n/a'}",
        f"- Last host_time: {metrics.last_host_time or 'n/a'}",
        f"- Approx duration (seconds): {metrics.approx_duration_seconds if metrics.approx_duration_seconds is not None else 'n/a'}",
        "",
        "## Channel metrics",
        "",
    ]

    if not metrics.channels:
        lines.append("- No channel telemetry rows found.")
    else:
        for channel in metrics.channels:
            lines.extend(
                [
                    f"### channel_{channel.channel_id}",
                    "",
                    f"- Source: {channel.source}",
                    f"- Samples: {channel.samples}",
                    f"- Min temperature: {channel.min_temp_c if channel.min_temp_c is not None else 'n/a'}",
                    f"- Max temperature: {channel.max_temp_c if channel.max_temp_c is not None else 'n/a'}",
                    f"- Status counts: {json.dumps(channel.statuses, ensure_ascii=False)}",
                    "",
                ]
            )

    lines.extend(
        [
            "## Review checklist",
            "",
            "- Confirm heartbeat continuity matches the expected capture duration.",
            "- Confirm `channel_0` remained stable when untouched.",
            "- Confirm `channel_1` continued cycling into `overtemp` as expected.",
            "- Confirm no unexpected parser or serial disconnect issue was logged during the run.",
        ]
    )
    return "\n".join(lines) + "\n"


def main() -> int:
    args = parse_args()
    run_dir = Path(args.run_dir)
    if not run_dir.exists():
        raise SystemExit(f"Run directory does not exist: {run_dir}")

    metrics = summarize_run(run_dir)
    summary_md = render_markdown(metrics)
    (run_dir / "stability_summary.md").write_text(summary_md, encoding="utf-8")
    (run_dir / "stability_metrics.json").write_text(
        json.dumps(asdict(metrics), ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    print(summary_md, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
