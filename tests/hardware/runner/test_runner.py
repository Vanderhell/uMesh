#!/usr/bin/env python3
"""
test_runner.py - uMesh Hardware Integration Test Runner

Supports two execution modes:
1) Full mode (default): coordinator + router + end_node serial ports
2) Coordinator-only mode: monitor/control only coordinator serial port

In both modes, runner uses host-driven control:
- sends READY to opened ports and waits for fresh ready events
- sends START to coordinator to trigger test suite
"""

import sys
import time
from collections import Counter
from dataclasses import dataclass
from typing import Dict, List

import click

from device import Device
from reporter import EXPECTED_TESTS, Reporter


@dataclass
class RunOutcome:
    run_index: int
    code: int
    reason: str
    missing_tests: List[str]
    failed_tests: List[str]


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
        click.echo("pyserial not installed - run: pip install pyserial")


def _event_counts(devices: Dict[str, Device]) -> Dict[str, Counter]:
    out: Dict[str, Counter] = {}
    for role, dev in devices.items():
        out[role] = Counter(ev.get("event", "?") for ev in dev.events())
    return out


def _run_once(
    run_index: int,
    devices: Dict[str, Device],
    timeout: int,
    ready_timeout: int,
    start_command: str,
    pre_start_delay: float,
) -> RunOutcome:
    reporter = Reporter()
    reporter_started = False

    for role, dev in devices.items():
        _role = role
        dev.on_any(lambda ev, r=_role: reporter.on_event(r, ev))

    click.echo("Opening serial ports...")
    failed_ports: List[str] = []
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
        return RunOutcome(run_index, 1, "open_failed", [], [])

    click.echo("\nAll requested ports open. Synchronizing devices...\n")
    time.sleep(0.5)

    for dev in devices.values():
        dev.clear_events()

    for role, dev in devices.items():
        try:
            dev.send_command("READY")
        except Exception as exc:
            click.echo(f"[SYNC] Failed to send READY to {role}: {exc}", err=True)
            for d in devices.values():
                d.stop()
            return RunOutcome(run_index, 3, "ready_command_failed", [], [])

    ready_deadline = time.monotonic() + ready_timeout
    while time.monotonic() < ready_deadline:
        if all(dev.events_of("ready") for dev in devices.values()):
            break
        time.sleep(0.2)

    missing_ready = [role for role, dev in devices.items() if not dev.events_of("ready")]
    if missing_ready:
        click.echo(
            "\n[SYNC] Missing fresh 'ready' from opened devices: "
            + ", ".join(missing_ready),
            err=True,
        )
        click.echo("\nObserved events before sync timeout:", err=True)
        counts_by_role = _event_counts(devices)
        for role in devices.keys():
            counts = counts_by_role.get(role, Counter())
            rendered = ", ".join(
                f"{name}={count}" for name, count in sorted(counts.items())
            ) if counts else "none"
            click.echo(f"  {role:12s} {rendered}", err=True)
        for dev in devices.values():
            dev.stop()
        return RunOutcome(run_index, 3, "ready_timeout", [], [])

    if pre_start_delay > 0:
        click.echo(f"[SYNC] Waiting {pre_start_delay:.1f}s before START...")
        time.sleep(pre_start_delay)

    click.echo(f"[SYNC] Sending start command to coordinator: {start_command}")
    try:
        devices["coordinator"].send_command(start_command)
    except Exception as exc:
        click.echo(f"[SYNC] Failed to send start command: {exc}", err=True)
        for dev in devices.values():
            dev.stop()
        return RunOutcome(run_index, 3, "start_command_failed", [], [])

    reporter.start()
    reporter_started = True

    deadline = time.monotonic() + timeout
    coord = devices["coordinator"]
    done = False

    try:
        while time.monotonic() < deadline:
            if coord.events_of("stats"):
                done = True
                break
            time.sleep(0.5)
    except KeyboardInterrupt:
        click.echo("\nInterrupted by user.")
    finally:
        if reporter_started:
            reporter.stop()
        for dev in devices.values():
            dev.stop()

    if not done:
        click.echo(
            f"\n[TIMEOUT] Coordinator did not emit 'stats' within {timeout}s.",
            err=True,
        )
        click.echo("\nObserved events by device:", err=True)
        counts_by_role = _event_counts(devices)
        for role in devices.keys():
            counts = counts_by_role.get(role, Counter())
            rendered = ", ".join(
                f"{name}={count}" for name, count in sorted(counts.items())
            ) if counts else "none"
            click.echo(f"  {role:12s} {rendered}", err=True)
        return RunOutcome(run_index, 2, "stats_timeout", [], [])

    latest_by_test = {}
    for ev in coord.events_of("test_result"):
        data = ev.get("data", {})
        name = data.get("test")
        if name:
            latest_by_test[name] = data

    missing = [name for name in EXPECTED_TESTS if name not in latest_by_test]
    failed = [
        name for name, data in latest_by_test.items()
        if name in EXPECTED_TESTS and not bool(data.get("pass", False))
    ]

    if missing:
        click.echo(
            "\n[INCOMPLETE] Missing test_result events for: " + ", ".join(missing),
            err=True,
        )
    if failed:
        click.echo(
            "\n[FAILED] Tests reported fail: " + ", ".join(failed),
            err=True,
        )

    code = 0 if not missing and not failed else 1
    reason = "ok" if code == 0 else "assertion_failed"
    return RunOutcome(run_index, code, reason, missing, failed)


@click.command()
@click.option("--coordinator", "-c", default=None,
              help="Serial port for coordinator (e.g. COM3 or /dev/ttyUSB0)")
@click.option("--router", "-r", default=None, help="Serial port for router")
@click.option("--end-node", "-e", default=None, help="Serial port for end_node")
@click.option("--coordinator-only", is_flag=True, default=False,
              help="Open/monitor only coordinator serial port")
@click.option("--runs", default=1, show_default=True,
              help="Number of repeated runs")
@click.option("--inter-run-delay", default=1.5, show_default=True,
              help="Delay in seconds between runs")
@click.option("--baud", "-b", default=115200, show_default=True,
              help="Serial baud rate")
@click.option("--timeout", "-t", default=120, show_default=True,
              help="Maximum seconds to wait for coordinator stats event")
@click.option("--ready-timeout", default=12, show_default=True,
              help="Seconds to wait for fresh ready after READY command")
@click.option("--start-command", default="START", show_default=True,
              help="Serial command sent to coordinator to start tests")
@click.option("--pre-start-delay", default=0.0, show_default=True,
              help="Delay in seconds after READY sync and before START")
@click.option("--list-ports", is_flag=True, default=False,
              help="List available serial ports and exit")
def main(
    coordinator: str,
    router: str,
    end_node: str,
    coordinator_only: bool,
    runs: int,
    inter_run_delay: float,
    baud: int,
    timeout: int,
    ready_timeout: int,
    start_command: str,
    pre_start_delay: float,
    list_ports: bool,
) -> None:
    """uMesh hardware integration test runner."""

    if list_ports:
        list_serial_ports()
        return

    if runs < 1:
        click.echo("Error: --runs must be >= 1", err=True)
        sys.exit(1)

    missing = []
    if not coordinator:
        missing.append("--coordinator")
    if not coordinator_only:
        if not router:
            missing.append("--router")
        if not end_node:
            missing.append("--end-node")
    if missing:
        click.echo(
            f"Error: missing required options: {', '.join(missing)}\n"
            "Run with --help for usage.",
            err=True,
        )
        sys.exit(1)

    outcomes: List[RunOutcome] = []

    for run_idx in range(1, runs + 1):
        click.echo("")
        click.echo(f"=== Run {run_idx}/{runs} ===")

        if coordinator_only:
            devices = {
                "coordinator": Device("coordinator", coordinator, baud),
            }
        else:
            devices = {
                "coordinator": Device("coordinator", coordinator, baud),
                "router": Device("router", router, baud),
                "end_node": Device("end_node", end_node, baud),
            }

        outcome = _run_once(
            run_index=run_idx,
            devices=devices,
            timeout=timeout,
            ready_timeout=ready_timeout,
            start_command=start_command,
            pre_start_delay=pre_start_delay,
        )
        outcomes.append(outcome)

        status = "PASS" if outcome.code == 0 else "FAIL"
        click.echo(f"[RUN {run_idx}] {status} ({outcome.reason})")

        if run_idx < runs and inter_run_delay > 0:
            time.sleep(inter_run_delay)

    if runs == 1:
        sys.exit(outcomes[0].code)

    passed = sum(1 for o in outcomes if o.code == 0)
    failed = runs - passed

    click.echo("\n=== Aggregate ===")
    click.echo(f"Runs: {runs}  Passed: {passed}  Failed: {failed}")
    for o in outcomes:
        click.echo(f"  run {o.run_index:02d}: code={o.code} reason={o.reason}")

    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
