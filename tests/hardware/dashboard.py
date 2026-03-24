#!/usr/bin/env python3
"""
dashboard.py — µMesh Live Device Dashboard

Real-time monitor for coordinator, router, and end_node.
Connects to ESP32 devices via USB serial, reads JSON events,
and displays live status, stats, and event log.

Usage:
    python dashboard.py --coordinator COM3 --router COM4 --end-node COM5
    python dashboard.py --list-ports

Any subset of devices can be connected (e.g. coordinator only).
"""

import sys
import time
import threading
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional, Dict, List, Tuple

import click
from rich.console import Console, Group
from rich.live import Live
from rich.panel import Panel
from rich.table import Table
from rich.text import Text
from rich.columns import Columns
from rich import box

# Device class lives next to this file
sys.path.insert(0, str(Path(__file__).resolve().parent / "runner"))
from device import Device  # noqa: E402


# ── Opcode name lookup ────────────────────────────────────────────────────────

CMD_NAMES: Dict[str, str] = {
    "0x01": "PING",         "0x02": "PONG",         "0x03": "RESET",
    "0x04": "STATUS",       "0x10": "SENSOR_TEMP",  "0x11": "SENSOR_HUM",
    "0x12": "SENSOR_PRESS", "0x13": "SENSOR_RAW",   "0x30": "SET_INTERVAL",
    "0x31": "SET_THRESHOLD","0x32": "SET_MODE",      "0x50": "JOIN",
    "0x51": "ASSIGN",       "0x52": "LEAVE",         "0x53": "DISCOVER",
    "0x54": "ROUTE_UPDATE", "0x55": "NODE_JOINED",   "0x56": "NODE_LEFT",
    "0xff": "RAW",
}

ROLE_COLORS = {
    "coordinator": "blue",
    "router":      "magenta",
    "end_node":    "cyan",
}

EVENT_COLORS = {
    "ready":       "cyan",
    "joined":      "green",
    "tx":          "bright_black",
    "rx":          "bright_black",
    "error":       "red",
    "stats":       "green",
    "test_result": "yellow",
    "left":        "yellow",
}

# ── Per-device state ──────────────────────────────────────────────────────────

@dataclass
class DeviceState:
    role: str
    port: Optional[str] = None
    alive: bool = False          # serial thread running

    # Filled by "ready" event
    node_id:  Optional[int] = None
    channel:  Optional[int] = None
    net_id:   Optional[int] = None
    fw_state: str = "—"          # state string from firmware

    # Live counters
    tx:     int = 0
    rx:     int = 0
    ack:    int = 0
    retry:  int = 0
    drop:   int = 0
    errors: int = 0

    last_rssi:  Optional[int] = None
    last_event: str = ""

    # Track JOIN sequence progress for this device
    join_sent:    bool = False
    assign_recvd: bool = False
    joined:       bool = False


# ── Dashboard ─────────────────────────────────────────────────────────────────

MAX_LOG = 200   # how many events to keep in memory
SHOW_LOG = 35   # how many to display at once


class Dashboard:
    """Thread-safe live dashboard."""

    def __init__(self) -> None:
        self._lock       = threading.Lock()
        self._start_time = time.monotonic()
        self._console    = Console()
        self._live: Optional[Live] = None

        self._states: Dict[str, DeviceState] = {
            role: DeviceState(role)
            for role in ("coordinator", "router", "end_node")
        }

        # log entries: (timestamp_s, role, event_type, formatted_data_str)
        self._log: deque = deque(maxlen=MAX_LOG)

    # ── Public ────────────────────────────────────────────────────────────

    def set_port(self, role: str, port: Optional[str]) -> None:
        with self._lock:
            self._states[role].port = port

    def mark_alive(self, role: str) -> None:
        with self._lock:
            self._states[role].alive = True

    def on_event(self, role: str, ev: dict) -> None:
        etype = ev.get("event", "")
        data  = ev.get("data", {})
        ts    = time.monotonic() - self._start_time

        with self._lock:
            s = self._states[role]
            s.alive      = True
            s.last_event = etype

            if etype == "ready":
                s.node_id  = data.get("node_id")
                s.channel  = data.get("channel")
                s.net_id   = data.get("net_id")
                s.fw_state = "ready"

            elif etype == "joined":
                s.joined   = True
                s.node_id  = data.get("node_id", s.node_id)
                s.fw_state = "joined"

            elif etype == "tx":
                s.tx += 1
                cmd = str(data.get("cmd", "")).lower()
                if cmd == "0x50":   # JOIN
                    s.join_sent = True

            elif etype == "rx":
                s.rx += 1
                if "rssi" in data:
                    s.last_rssi = data["rssi"]
                cmd = str(data.get("cmd", "")).lower()
                if cmd == "0x51":   # ASSIGN
                    s.assign_recvd = True

            elif etype == "error":
                s.errors  += 1
                msg        = data.get("msg", "?")
                s.fw_state = f"ERR:{msg}"

            elif etype == "stats":
                s.tx    = data.get("tx",    s.tx)
                s.rx    = data.get("rx",    s.rx)
                s.ack   = data.get("ack",   s.ack)
                s.retry = data.get("retry", s.retry)
                s.drop  = data.get("drop",  s.drop)
                s.fw_state = "done"

            # Build compact data string for log
            parts = []
            for k, v in data.items():
                if k == "cmd":
                    name = CMD_NAMES.get(str(v).lower(), str(v))
                    parts.append(f"cmd={name}")
                elif k == "rssi":
                    parts.append(f"rssi={v} dBm")
                else:
                    parts.append(f"{k}={v}")
            data_str = "  ".join(parts)

            self._log.append((ts, role, etype, data_str))

        if self._live:
            self._live.update(self._render())

    def start(self) -> None:
        self._start_time = time.monotonic()
        self._live = Live(
            self._render(),
            console=self._console,
            refresh_per_second=4,
            vertical_overflow="visible",
        )
        self._live.__enter__()

    def stop(self) -> None:
        if self._live:
            self._live.update(self._render())
            self._live.__exit__(None, None, None)

    # ── Rendering ─────────────────────────────────────────────────────────

    def _device_panel(self, role: str) -> Panel:
        s   = self._states[role]
        rc  = ROLE_COLORS[role]

        tbl = Table(box=None, show_header=False, padding=(0, 1), expand=True)
        tbl.add_column(style="dim", min_width=10)
        tbl.add_column()

        if not s.port:
            tbl.add_row("Port", "[dim]not configured[/dim]")
            border = "dim"

        elif not s.alive:
            tbl.add_row("Port",  f"[yellow]{s.port}[/yellow]")
            tbl.add_row("State", "[yellow]connecting...[/yellow]")
            border = "yellow"

        else:
            tbl.add_row("Port", f"[dim]{s.port}[/dim]")

            # State
            state_color = {
                "ready":  "cyan",
                "joined": "green",
                "done":   "green",
                "—":      "dim",
            }.get(s.fw_state.split(":")[0], "yellow")
            tbl.add_row("State", f"[{state_color}]{s.fw_state}[/{state_color}]")

            # Identity
            nid = f"0x{s.node_id:02X}" if s.node_id is not None else "[dim]?[/dim]"
            ch  = str(s.channel)        if s.channel  is not None else "[dim]?[/dim]"
            net = str(s.net_id)         if s.net_id   is not None else "[dim]?[/dim]"
            tbl.add_row("Node ID", nid)
            tbl.add_row("Channel", ch)
            tbl.add_row("Net ID",  net)
            tbl.add_row("", "")

            # Traffic stats
            tbl.add_row("TX  / RX",    f"{s.tx} / {s.rx}")
            tbl.add_row("ACK / Retry", f"{s.ack} / {s.retry}")
            err_color = "red" if (s.drop or s.errors) else "dim"
            tbl.add_row(
                "Drop / Err",
                f"[{err_color}]{s.drop} / {s.errors}[/{err_color}]",
            )

            # RSSI
            if s.last_rssi is not None:
                rssi  = s.last_rssi
                rcolor = (
                    "green"  if rssi > -50 else
                    "yellow" if rssi > -70 else
                    "red"
                )
                tbl.add_row("Last RSSI", f"[{rcolor}]{rssi} dBm[/{rcolor}]")

            tbl.add_row("", "")

            # JOIN sequence progress
            def tick(ok: bool) -> str:
                return "[green]✓[/green]" if ok else "[dim]·[/dim]"

            tbl.add_row(
                "JOIN seq",
                f"{tick(s.join_sent)} sent  "
                f"{tick(s.assign_recvd)} assign  "
                f"{tick(s.joined)} joined",
            )

            border = rc

        title = f"[bold {rc}]{role.upper().replace('_', ' ')}[/bold {rc}]"
        return Panel(tbl, title=title, border_style=border)

    def _log_panel(self) -> Panel:
        with self._lock:
            entries: List[Tuple] = list(self._log)[-SHOW_LOG:]

        text = Text(overflow="fold")
        for ts, role, etype, data_str in entries:
            mins = int(ts) // 60
            secs = ts % 60
            rc   = ROLE_COLORS.get(role, "white")
            ec   = EVENT_COLORS.get(etype, "white")

            text.append(f"[{mins:02d}:{secs:05.2f}] ", style="dim")
            text.append(f"{role:11s} ", style=rc)
            text.append(f"{etype:<14s}", style=ec)
            if data_str:
                text.append(data_str, style="dim")
            text.append("\n")

        return Panel(
            text,
            title="[bold]Event Log[/bold]",
            border_style="dim",
            subtitle=f"[dim]{len(list(self._log))} events[/dim]",
        )

    def _render(self) -> Panel:
        elapsed = time.monotonic() - self._start_time

        with self._lock:
            states = dict(self._states)

        device_panels = Columns(
            [self._device_panel(r) for r in ("coordinator", "router", "end_node")],
            equal=True,
            expand=True,
        )

        content = Group(device_panels, self._log_panel())

        title = (
            f"[bold yellow]µMesh Dashboard[/bold yellow]"
            f"  [dim]elapsed: {elapsed:.1f}s   Ctrl+C to quit[/dim]"
        )
        return Panel(content, title=title, border_style="yellow")


# ── CLI ───────────────────────────────────────────────────────────────────────

def list_serial_ports() -> None:
    try:
        import serial.tools.list_ports as lp
        ports = list(lp.comports())
        if not ports:
            click.echo("No serial ports found.")
            return
        click.echo("Available serial ports:")
        for p in ports:
            click.echo(f"  {p.device:14s}  {p.description}")
    except ImportError:
        click.echo("pyserial not installed — run: pip install pyserial")


@click.command()
@click.option("--coordinator", "-c", default=None,
              metavar="PORT", help="Serial port for coordinator (e.g. COM3)")
@click.option("--router",      "-r", default=None,
              metavar="PORT", help="Serial port for router")
@click.option("--end-node",    "-e", default=None,
              metavar="PORT", help="Serial port for end_node")
@click.option("--baud",        "-b", default=115200, show_default=True,
              help="Serial baud rate")
@click.option("--request-ready", is_flag=True, default=False,
              help="Send READY command to connected devices after opening ports")
@click.option("--start-tests", is_flag=True, default=False,
              help="Send START command to coordinator after opening ports")
@click.option("--list-ports",  is_flag=True, default=False,
              help="List available serial ports and exit")
def main(coordinator: str, router: str, end_node: str,
         baud: int, request_ready: bool, start_tests: bool,
         list_ports: bool) -> None:
    """µMesh live device dashboard — monitor all 3 nodes in real time."""

    if list_ports:
        list_serial_ports()
        return

    role_ports = {
        "coordinator": coordinator,
        "router":      router,
        "end_node":    end_node,
    }

    configured = {role: port for role, port in role_ports.items() if port}
    if not configured:
        click.echo(
            "No serial ports specified.\n"
            "Example: python dashboard.py --coordinator COM3 --router COM4\n"
            "Run with --list-ports to see available ports.",
            err=True,
        )
        sys.exit(1)

    dash = Dashboard()
    devices: Dict[str, Device] = {}

    # Set ports in dashboard (so unconfigured slots show "not configured")
    for role, port in role_ports.items():
        dash.set_port(role, port)

    # Open configured ports
    click.echo("Opening serial ports...")
    failed = []
    for role, port in configured.items():
        dev = Device(role, port, baud)
        dev.on_any(lambda ev, r=role: dash.on_event(r, ev))
        try:
            dev.start()
            dash.mark_alive(role)
            click.echo(f"  {role:12s}  {port}  OK")
        except Exception as exc:
            click.echo(f"  {role:12s}  {port}  FAILED: {exc}", err=True)
            failed.append(role)
            continue
        devices[role] = dev

    if failed and not devices:
        click.echo("\nNo devices could be opened. Check connections.", err=True)
        sys.exit(1)

    if failed:
        click.echo(
            f"\n[!] Could not open: {', '.join(failed)} — continuing with {len(devices)} device(s)."
        )

    if request_ready or start_tests:
        for dev in devices.values():
            dev.clear_events()

    if request_ready:
        click.echo("Sending READY command to connected devices...")
        for role, dev in devices.items():
            try:
                dev.send_command("READY")
                click.echo(f"  {role:12s}  READY  OK")
            except Exception as exc:
                click.echo(f"  {role:12s}  READY  FAILED: {exc}", err=True)

    if start_tests:
        coord = devices.get("coordinator")
        if not coord:
            click.echo("Cannot send START: coordinator port is not connected.", err=True)
        else:
            try:
                coord.send_command("START")
                click.echo("Sent START to coordinator.")
            except Exception as exc:
                click.echo(f"Failed to send START: {exc}", err=True)

    click.echo(f"\nMonitoring {len(devices)} device(s)...\n")
    time.sleep(0.3)

    dash.start()
    try:
        while True:
            time.sleep(0.2)
            # Stop if all serial threads died (devices unplugged)
            if devices and all(not d.is_alive() for d in devices.values()):
                break
    except KeyboardInterrupt:
        pass
    finally:
        dash.stop()
        for dev in devices.values():
            dev.stop()

    click.echo("\nDashboard closed.")


if __name__ == "__main__":
    main()
