"""Terminal dashboard for the Crowdy STM32 sensor stream.

Reads lines from the Nucleo's ST-Link VCP (UART2 @ 115200) and renders sensor
values, per-sensor connection status, and fused occupancy level live in the
terminal using `rich`.

Usage:
    python monitor.py --port COM5
    python monitor.py --port /dev/ttyACM0 --baud 115200
    python monitor.py --replay capture.log         # offline playback of a saved log

Dependencies:
    pip install pyserial rich
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Iterable, Optional

from rich.align import Align
from rich.console import Group
from rich.layout import Layout
from rich.live import Live
from rich.panel import Panel
from rich.table import Table
from rich.text import Text

LINE_RE = re.compile(
    r"MIC raw:\s*(?P<mic_raw>\d+)\s+rms:\s*(?P<mic_rms>[\d.]+)\s*\|\s*"
    r"VIB raw:\s*(?P<vib_raw>\d+)\s+avg:\s*(?P<vib_avg>[\d.]+)\s*\|\s*"
    r"RADAR (?P<radar_presence>ON|OFF)\s+raw:\s*(?P<radar_raw>\d+)\s+avg:\s*(?P<radar_avg>\d+)\s*\|\s*"
    r"M:(?P<m_lvl>.)\s*V:(?P<v_lvl>.)\s*R:(?P<r_lvl>.)\s*=>\s*(?P<fused>\w+)"
    r"(?:\s*\[WARNING:[^\]]*\])?"
)

LEVEL_STYLE = {
    "EMPTY":  ("dim white",    "·"),
    "LOW":    ("green",        "▂"),
    "MEDIUM": ("yellow",       "▅"),
    "HIGH":   ("bold red",     "█"),
}


@dataclass
class SensorState:
    name: str
    raw: Optional[int] = None
    processed: Optional[float] = None
    processed_unit: str = ""
    level_char: str = "-"
    extra: str = ""            # e.g. radar "ON"/"OFF"

    @property
    def connected(self) -> bool:
        return self.level_char != "-"


@dataclass
class DashboardState:
    port_label: str
    mic: SensorState = field(default_factory=lambda: SensorState("Microphone"))
    vib: SensorState = field(default_factory=lambda: SensorState("Vibration"))
    rad: SensorState = field(default_factory=lambda: SensorState("Radar"))
    fused: str = "—"
    last_line: str = ""
    last_update: Optional[datetime] = None
    lines_seen: int = 0
    parse_failures: int = 0
    all_disconnected: bool = False
    disconnect_events: int = 0   # transitions into any-sensor-disconnected state

    def sensors(self) -> Iterable[SensorState]:
        return (self.mic, self.vib, self.rad)

    def disconnected_now(self) -> int:
        if self.all_disconnected:
            return 3
        return sum(1 for s in self.sensors() if not s.connected)


def parse_line(line: str, state: DashboardState) -> bool:
    line = line.strip()
    if not line:
        return False
    state.last_line = line
    state.last_update = datetime.now()
    state.lines_seen += 1

    prev_disconnect_any = (state.all_disconnected or
                           any(not s.connected for s in state.sensors()))

    if "All sensors disconnected" in line:
        state.all_disconnected = True
        state.fused = "—"
        for s in state.sensors():
            s.level_char = "-"
            s.raw = None
            s.processed = None
        if not prev_disconnect_any:
            state.disconnect_events += 1
        return True

    m = LINE_RE.search(line)
    if not m:
        state.parse_failures += 1
        return False

    state.all_disconnected = False
    state.mic.raw = int(m["mic_raw"])
    state.mic.processed = float(m["mic_rms"])
    state.mic.processed_unit = "rms"
    state.mic.level_char = m["m_lvl"]

    state.vib.raw = int(m["vib_raw"])
    state.vib.processed = float(m["vib_avg"])
    state.vib.processed_unit = "avg"
    state.vib.level_char = m["v_lvl"]

    state.rad.raw = int(m["radar_raw"])
    state.rad.processed = float(m["radar_avg"])
    state.rad.processed_unit = "cm"
    state.rad.level_char = m["r_lvl"]
    state.rad.extra = m["radar_presence"]

    state.fused = m["fused"].upper()

    cur_disconnect_any = any(not s.connected for s in state.sensors())
    if cur_disconnect_any and not prev_disconnect_any:
        state.disconnect_events += 1
    return True


def level_from_char(ch: str) -> str:
    return {"0": "EMPTY", "1": "LOW", "2": "MEDIUM", "3": "HIGH"}.get(ch, "—")


def render_header(state: DashboardState) -> Panel:
    style, glyph = LEVEL_STYLE.get(state.fused, ("white", "?"))
    occ = Text()
    occ.append("Occupancy: ", style="bold")
    occ.append(f"{glyph} {state.fused}", style=style)

    left = Text()
    left.append("DNES Crowdy Monitor  ", style="bold cyan")
    left.append(f"[{state.port_label}]", style="dim")

    grid = Table.grid(expand=True)
    grid.add_column(justify="left")
    grid.add_column(justify="right")
    grid.add_row(left, occ)
    return Panel(grid, border_style="cyan")


def render_sensor_table(state: DashboardState) -> Panel:
    table = Table(expand=True, show_edge=False, pad_edge=False)
    table.add_column("Sensor",    style="bold")
    table.add_column("Status",    justify="center")
    table.add_column("Raw",       justify="right")
    table.add_column("Processed", justify="right")
    table.add_column("Level",     justify="center")

    for s in state.sensors():
        if s.connected:
            status = Text("● OK", style="green")
        else:
            status = Text("● DISC", style="bold red")

        raw_s = "—" if s.raw is None else f"{s.raw}"
        if s.processed is None:
            proc_s = "—"
        else:
            proc_s = f"{s.processed:.2f} {s.processed_unit}".strip()
        if s.name == "Radar" and s.extra:
            proc_s = f"{proc_s}  ({s.extra})"

        lvl_name = level_from_char(s.level_char)
        lvl_style, glyph = LEVEL_STYLE.get(lvl_name, ("dim", "—"))
        if not s.connected:
            level_cell = Text("—", style="dim")
        else:
            level_cell = Text(f"{glyph} {lvl_name} ({s.level_char})", style=lvl_style)

        table.add_row(s.name, status, raw_s, proc_s, level_cell)

    return Panel(table, title="Sensors", border_style="white")


def render_status(state: DashboardState) -> Panel:
    disc_now = state.disconnected_now()
    disc_text = Text()
    disc_text.append("Disconnected now: ", style="bold")
    style = "green" if disc_now == 0 else ("yellow" if disc_now < 3 else "bold red")
    disc_text.append(f"{disc_now}/3", style=style)
    disc_text.append(f"    Disconnect events: {state.disconnect_events}", style="dim")

    stats = Text()
    stats.append(f"Lines: {state.lines_seen}", style="dim")
    if state.parse_failures:
        stats.append(f"   Parse failures: {state.parse_failures}", style="yellow")
    if state.last_update is not None:
        stats.append(f"   Last: {state.last_update.strftime('%H:%M:%S')}", style="dim")

    last = Text()
    last.append("Last line: ", style="bold dim")
    tail = state.last_line if len(state.last_line) <= 180 else state.last_line[:177] + "..."
    last.append(tail or "(waiting for data...)", style="white")

    return Panel(Group(disc_text, stats, last),
                 title="Status", border_style="white")


def build_layout(state: DashboardState) -> Layout:
    layout = Layout()
    layout.split_column(
        Layout(render_header(state),       size=3,  name="header"),
        Layout(render_sensor_table(state),          name="sensors"),
        Layout(render_status(state),       size=5,  name="status"),
    )
    return layout


# --- input sources ---------------------------------------------------------

def iter_serial(port: str, baud: int):
    try:
        import serial  # type: ignore
    except ImportError:
        print("pyserial not installed. Run: pip install pyserial rich", file=sys.stderr)
        sys.exit(1)

    ser = serial.Serial(port, baud, timeout=0.5)
    try:
        buf = b""
        while True:
            chunk = ser.read(256)
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    yield line.decode("ascii", errors="replace")
            else:
                yield None   # tick so the dashboard stays responsive
    finally:
        ser.close()


def iter_replay(path: Path, speed: float = 20.0):
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            yield line.rstrip("\r\n")
            time.sleep(1.0 / speed)
    while True:
        yield None
        time.sleep(0.1)


# --- entrypoint ------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description="Crowdy terminal dashboard")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--port",   help="Serial port (e.g. COM5, /dev/ttyACM0)")
    src.add_argument("--replay", type=Path, help="Replay lines from a log file instead of reading a serial port")
    ap.add_argument("--baud",    type=int, default=115200)
    ap.add_argument("--replay-speed", type=float, default=20.0,
                    help="Lines per second when replaying a log (default: 20)")
    args = ap.parse_args()

    if args.port:
        label = f"{args.port} @ {args.baud}"
        source = iter_serial(args.port, args.baud)
    else:
        label = f"replay:{args.replay.name}"
        source = iter_replay(args.replay, args.replay_speed)

    state = DashboardState(port_label=label)

    with Live(build_layout(state), refresh_per_second=8, screen=True) as live:
        try:
            for item in source:
                if item is not None:
                    parse_line(item, state)
                live.update(build_layout(state))
        except KeyboardInterrupt:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
