"""
DNES terminal-side occupancy classifier / firmware verifier.

Reads the STM32 CSV stream
    t_ms, acoustic, vibration, presence, range, ci_milli, class
over USB serial, runs an EWMA smoother + weighted Congestion Index
(mirror of sensor_read/occupancy.c), classifies into EMPTY/LOW/MED/HIGH,
and renders a live dashboard. The firmware reports its own CI and class
on every line — both are shown so you can confirm the board matches the
Python reference after retuning.

Use --replay to iterate on weights against a previously logged CSV
without the board attached.
"""

from __future__ import annotations

import argparse
import sys
import time
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterator, Optional, Tuple

from rich.align import Align
from rich.console import Console, Group
from rich.live import Live
from rich.panel import Panel
from rich.table import Table
from rich.text import Text

ADC_MAX = 4095.0

CLASS_COLORS = {
    "EMPTY": "white",
    "LOW":   "green",
    "MED":   "yellow",
    "HIGH":  "red",
}

FW_CLASS = {"E": "EMPTY", "L": "LOW", "M": "MED", "H": "HIGH"}


@dataclass
class Config:
    port: Optional[str]
    baud: int
    replay: Optional[Path]
    replay_speed: float
    log_path: Optional[Path]
    lam: float
    alpha: float
    beta: float
    gamma: float
    t_low: float
    t_med: float
    t_high: float
    calib: int
    stuck_samples: int


@dataclass
class State:
    t_ms: int = 0
    a_raw: int = 0
    v_raw: int = 0
    p_raw: int = 0
    r_raw: int = -1          # mmWave range; -1 = no target
    fw_ci: float = 0.0       # firmware-reported CI (ci_milli/1000)
    fw_cls: str = "EMPTY"    # firmware-reported class
    a_ewma: float = 0.0
    v_ewma: float = 0.0
    p_ewma: float = 0.0
    a_norm: float = 0.0
    v_norm: float = 0.0
    ci: float = 0.0
    cls: str = "EMPTY"
    n: int = 0
    calibrating: bool = True
    a_base: float = 0.0
    v_base: float = 0.0
    a_buf: deque = field(default_factory=lambda: deque(maxlen=1))
    v_buf: deque = field(default_factory=lambda: deque(maxlen=1))
    initialised: bool = False
    a_last_change: int = 0   # value of s.n when raw last differed
    v_last_change: int = 0
    r_last_change: int = 0


def parse_args() -> Config:
    ap = argparse.ArgumentParser(description="DNES occupancy dashboard")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="Serial port (e.g. COM5 or /dev/ttyACM0)")
    src.add_argument("--replay", type=Path, help="Read from a logged CSV instead of serial")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--replay-speed", type=float, default=1.0,
                    help="Replay speed multiplier (0 = as fast as possible)")
    ap.add_argument("--log", type=Path, default=None,
                    help="Append raw CSV lines to this file (live mode only)")
    ap.add_argument("--lam", type=float, default=0.2, help="EWMA lambda (0<lam<=1)")
    ap.add_argument("--alpha", type=float, default=0.5, help="Radar/presence weight")
    ap.add_argument("--beta",  type=float, default=0.3, help="Vibration weight")
    ap.add_argument("--gamma", type=float, default=0.2, help="Acoustic weight")
    ap.add_argument("--t-low",  type=float, default=0.15)
    ap.add_argument("--t-med",  type=float, default=0.40)
    ap.add_argument("--t-high", type=float, default=0.70)
    ap.add_argument("--calib", type=int, default=30,
                    help="Samples used to learn acoustic/vibration noise floor")
    ap.add_argument("--stuck-samples", type=int, default=30,
                    help="Flag a sensor as missing if its raw value hasn't "
                         "changed for this many samples (30 = 3 s at 10 Hz)")
    a = ap.parse_args()
    return Config(
        port=a.port, baud=a.baud,
        replay=a.replay, replay_speed=a.replay_speed,
        log_path=a.log,
        lam=a.lam, alpha=a.alpha, beta=a.beta, gamma=a.gamma,
        t_low=a.t_low, t_med=a.t_med, t_high=a.t_high,
        calib=a.calib,
        stuck_samples=a.stuck_samples,
    )


def classify(ci: float, cfg: Config) -> str:
    if ci < cfg.t_low:
        return "EMPTY"
    if ci < cfg.t_med:
        return "LOW"
    if ci < cfg.t_high:
        return "MED"
    return "HIGH"


def bar(value: float, width: int) -> str:
    value = max(0.0, min(1.0, value))
    filled = int(round(value * width))
    return "█" * filled + "░" * (width - filled)


def serial_source(cfg: Config) -> Iterator[str]:
    import serial  # local import so replay-only users don't need it installed
    ser = serial.Serial(cfg.port, cfg.baud, timeout=1)
    log_fp = open(cfg.log_path, "a", buffering=1, encoding="utf-8") if cfg.log_path else None
    try:
        while True:
            raw = ser.readline()
            if not raw:
                continue
            text = raw.decode("ascii", errors="replace").strip()
            if not text:
                continue
            if log_fp is not None and not text.startswith("#"):
                log_fp.write(text + "\n")
            yield text
    finally:
        ser.close()
        if log_fp is not None:
            log_fp.close()


def replay_source(cfg: Config) -> Iterator[str]:
    assert cfg.replay is not None
    prev_t: Optional[int] = None
    with cfg.replay.open("r", encoding="utf-8") as fp:
        for line in fp:
            text = line.strip()
            if not text:
                continue
            if cfg.replay_speed > 0 and not text.startswith("#"):
                head = text.split(",", 1)[0]
                try:
                    t_ms = int(head)
                except ValueError:
                    prev_t = None
                else:
                    if prev_t is not None and t_ms > prev_t:
                        dt = (t_ms - prev_t) / 1000.0 / cfg.replay_speed
                        time.sleep(dt)
                    prev_t = t_ms
            yield text


def parse_line(text: str) -> Optional[Tuple[int, int, int, int, int, int, str]]:
    if text.startswith("#"):
        return None
    parts = text.split(",")
    if len(parts) < 7:
        return None
    try:
        t_ms      = int(parts[0])
        acoustic  = int(parts[1])
        vibration = int(parts[2])
        presence  = int(parts[3])
        rng       = int(parts[4])
        ci_milli  = int(parts[5])
    except ValueError:
        return None
    cls_ch = parts[6].strip()[:1]
    if cls_ch not in FW_CLASS:
        return None
    return t_ms, acoustic, vibration, presence, rng, ci_milli, cls_ch


def update_state(
    s: State,
    sample: Tuple[int, int, int, int, int, int, str],
    cfg: Config,
) -> None:
    t_ms, acoustic, vibration, presence, rng, ci_milli, cls_ch = sample

    if not s.initialised:
        s.a_ewma, s.v_ewma, s.p_ewma = float(acoustic), float(vibration), float(presence)
        s.a_buf = deque(maxlen=cfg.calib)
        s.v_buf = deque(maxlen=cfg.calib)
        s.initialised = True
    else:
        s.a_ewma = cfg.lam * acoustic  + (1 - cfg.lam) * s.a_ewma
        s.v_ewma = cfg.lam * vibration + (1 - cfg.lam) * s.v_ewma
        s.p_ewma = cfg.lam * presence  + (1 - cfg.lam) * s.p_ewma

    s.a_buf.append(acoustic)
    s.v_buf.append(vibration)
    s.calibrating = len(s.a_buf) < cfg.calib
    if not s.calibrating:
        s.a_base = sum(s.a_buf) / len(s.a_buf)
        s.v_base = sum(s.v_buf) / len(s.v_buf)

    def norm(ewma_val: float, base: float) -> float:
        denom = ADC_MAX - base
        if denom <= 0.0:
            return 0.0
        return max(0.0, min(1.0, (ewma_val - base) / denom))

    s.a_norm = 0.0 if s.calibrating else norm(s.a_ewma, s.a_base)
    s.v_norm = 0.0 if s.calibrating else norm(s.v_ewma, s.v_base)
    p_norm = max(0.0, min(1.0, s.p_ewma))

    s.ci = cfg.alpha * p_norm + cfg.beta * s.v_norm + cfg.gamma * s.a_norm
    s.cls = classify(s.ci, cfg) if not s.calibrating else "EMPTY"

    if s.n == 0 or acoustic  != s.a_raw: s.a_last_change = s.n
    if s.n == 0 or vibration != s.v_raw: s.v_last_change = s.n
    if s.n == 0 or rng       != s.r_raw: s.r_last_change = s.n

    s.t_ms = t_ms
    s.a_raw, s.v_raw, s.p_raw = acoustic, vibration, presence
    s.r_raw = rng
    s.fw_ci = ci_milli / 1000.0
    s.fw_cls = FW_CLASS[cls_ch]
    s.n += 1


def render(s: State, cfg: Config) -> Panel:
    color = CLASS_COLORS[s.cls]

    big = Text(s.cls.center(40), style=f"bold {color}")

    def health_tag(last_change: int) -> str:
        if s.n < cfg.stuck_samples:
            return ""  # not enough data yet — stay quiet during warmup
        if (s.n - last_change) >= cfg.stuck_samples:
            return "  [red bold]MISSING?[/red bold]"
        return "  [green]ok[/green]"

    tbl = Table.grid(padding=(0, 2))
    tbl.add_column(justify="right", style="cyan", no_wrap=True)
    tbl.add_column(justify="left", no_wrap=True)
    tbl.add_row(
        "Acoustic",
        f"{s.a_raw:4d}  ewma={s.a_ewma:7.1f}  base={s.a_base:6.1f}  "
        f"[{bar(s.a_norm, 20)}] {s.a_norm:.2f}"
        + health_tag(s.a_last_change),
    )
    tbl.add_row(
        "Vibration",
        f"{s.v_raw:4d}  ewma={s.v_ewma:7.1f}  base={s.v_base:6.1f}  "
        f"[{bar(s.v_norm, 20)}] {s.v_norm:.2f}"
        + health_tag(s.v_last_change),
    )
    tbl.add_row(
        "Presence",
        f"{s.p_raw:4d}  ewma={s.p_ewma:5.2f}                 "
        f"[{bar(s.p_ewma, 20)}] {s.p_ewma:.2f}",
    )
    range_str = "no target" if s.r_raw < 0 else f"{s.r_raw}"
    tbl.add_row("Range (mmWave)", range_str + health_tag(s.r_last_change))

    ci_line = Text(f"CI = {s.ci:.3f}  [{bar(s.ci, 40)}]", style=f"bold {color}")

    fw_color = CLASS_COLORS[s.fw_cls]
    fw_line = Text.assemble(
        ("firmware   ", "dim"),
        (f"{s.fw_cls:<5}", f"bold {fw_color}"),
        ("   CI=", "dim"),
        (f"{s.fw_ci:.3f}", fw_color),
        ("   Δ(py−fw)=", "dim"),
        (f"{s.ci - s.fw_ci:+.3f}", "dim"),
    )

    weights = Text(
        f"alpha={cfg.alpha:.2f}  beta={cfg.beta:.2f}  gamma={cfg.gamma:.2f}   "
        f"lam={cfg.lam:.2f}",
        style="dim",
    )
    thresh = Text(
        f"thresholds   LOW>={cfg.t_low:.2f}   MED>={cfg.t_med:.2f}   HIGH>={cfg.t_high:.2f}",
        style="dim",
    )
    status = Text(
        f"t={s.t_ms} ms   samples={s.n}   "
        + ("calibrating…" if s.calibrating else "live"),
        style="dim",
    )

    body = Group(
        Align.center(big),
        Text(""),
        Align.center(ci_line),
        Align.center(fw_line),
        Text(""),
        tbl,
        Text(""),
        weights,
        thresh,
        status,
    )
    return Panel(body, title="DNES Occupancy", border_style=color)


def main() -> int:
    cfg = parse_args()
    console = Console()

    if cfg.replay is not None:
        if not cfg.replay.exists():
            console.print(f"[red]Replay file not found: {cfg.replay}[/red]")
            return 1
        source = replay_source(cfg)
    else:
        try:
            source = serial_source(cfg)
        except Exception as e:  # serial import or open failure
            console.print(f"[red]Serial error: {e}[/red]")
            return 1

    state = State()
    try:
        with Live(render(state, cfg), console=console,
                  refresh_per_second=10, screen=True) as live:
            for text in source:
                sample = parse_line(text)
                if sample is None:
                    continue
                update_state(state, sample, cfg)
                live.update(render(state, cfg))
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
