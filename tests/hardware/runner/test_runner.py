#!/usr/bin/env python3
"""
test_runner.py — µMesh Hardware Integration Test Runner

Connects to coordinator, router, and end_node over USB serial,
reads JSON events from each device, displays a live rich table,
and exits when the coordinator emits the final "stats" event.

Usage:
    python test_runner.py \\
        --coordinator COM3 \\
        --router      COM4 \\
        --end-node    COM5

    # List available ports:
    python test_runner.py --list-ports

    # Custom baud rate (default 115200):
    python test_runner.py --coordinator COM3 --router COM4 \\
        --end-node COM5 --baud 115200
"""

import sys
import time

import click

from device   import Device
from reporter import Reporter


def list_serial_ports() -> None:
    try:
        import serial.tools.list_ports as lp
        ports = list(lp.comports())
        if not ports:
            click.echo("No serial ports found.")
            return
        click.echo("Available serial ports:")
        for p in ports:
            click.echo(f"  {p.device:12s}  {p.description}")
    except ImportError:
        click.echo("pyserial not installed — run: pip install pyserial")


@click.command()
@click.option("--coordinator", "-c", default=None,
              help="Serial port for coordinator (e.g. COM3 or /dev/ttyUSB0)")
@click.option("--router",      "-r", default=None,
              help="Serial port for router")
@click.option("--end-node",    "-e", default=None,
              help="Serial port for end_node")
@click.option("--baud",        "-b", default=115200, show_default=True,
              help="Serial baud rate")
@click.option("--timeout",     "-t", default=120, show_default=True,
              help="Maximum seconds to wait for coordinator stats event")
@click.option("--list-ports",  is_flag=True, default=False,
              help="List available serial ports and exit")
def main(coordinator: str, router: str, end_node: str,
         baud: int, timeout: int, list_ports: bool) -> None:
    """µMesh hardware integration test runner."""

    if list_ports:
        list_serial_ports()
        return

    # ── Validate arguments ──────────────────────────────────────────────

    missing = []
    if not coordinator: missing.append("--coordinator")
    if not router:      missing.append("--router")
    if not end_node:    missing.append("--end-node")
    if missing:
        click.echo(
            f"Error: missing required options: {', '.join(missing)}\n"
            "Run with --help for usage.",
            err=True,
        )
        sys.exit(1)

    # ── Create devices ──────────────────────────────────────────────────

    devices = {
        "coordinator": Device("coordinator", coordinator, baud),
        "router":      Device("router",      router,      baud),
        "end_node":    Device("end_node",    end_node,    baud),
    }

    reporter = Reporter()

    # Wire each device's events to the reporter
    for role, dev in devices.items():
        _role = role  # capture for closure
        dev.on_any(lambda ev, r=_role: reporter.on_event(r, ev))

    # ── Open serial ports ───────────────────────────────────────────────

    click.echo("Opening serial ports...")
    failed_ports = []
    for role, dev in devices.items():
        try:
            dev.start()
            click.echo(f"  {role:12s}  {dev.port}  OK")
        except Exception as exc:
            click.echo(f"  {role:12s}  {dev.port}  FAILED: {exc}", err=True)
            failed_ports.append(role)

    if failed_ports:
        click.echo(
            f"\nCould not open ports for: {', '.join(failed_ports)}\n"
            "Check connections and try again.",
            err=True,
        )
        for dev in devices.values():
            dev.stop()
        sys.exit(1)

    click.echo("\nAll ports open. Starting tests...\n")
    time.sleep(0.5)

    # ── Live display ────────────────────────────────────────────────────

    reporter.start()

    deadline = time.monotonic() + timeout
    coord    = devices["coordinator"]
    done     = False

    try:
        while time.monotonic() < deadline:
            # Coordinator emits "stats" as the final event
            if coord.events_of("stats"):
                done = True
                break
            time.sleep(0.5)
    except KeyboardInterrupt:
        click.echo("\nInterrupted by user.")
    finally:
        reporter.stop()
        for dev in devices.values():
            dev.stop()

    if not done:
        click.echo(
            f"\n[TIMEOUT] Coordinator did not emit 'stats' within "
            f"{timeout}s.\nCheck that firmware is running and all nodes joined.",
            err=True,
        )
        sys.exit(2)

    # Exit code: 0 if all tests passed, 1 if any failed
    results = coord.events_of("test_result")
    any_fail = any(not ev.get("data", {}).get("pass", False)
                   for ev in results)
    sys.exit(1 if any_fail else 0)


if __name__ == "__main__":
    main()
