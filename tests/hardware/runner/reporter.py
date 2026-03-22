"""
reporter.py — Rich-formatted terminal display and results aggregation.

Maintains a live table of test results and per-device event counters.
Call Reporter.update() from any thread; it is thread-safe.
"""

import threading
import time
from typing import Dict, List, Optional

from rich.console import Console
from rich.live import Live
from rich.table import Table
from rich.panel import Panel
from rich.columns import Columns
from rich import box


# ── Result record ─────────────────────────────────────────────────────────

class TestResult:
    def __init__(self, name: str):
        self.name        = name
        self.pass_       = None   # True / False / None (pending)
        self.latency_ms  = -1
        self.delivered   = 0
        self.total       = 0

    @property
    def status_str(self) -> str:
        if self.pass_ is None:
            return "[dim]pending[/dim]"
        return "[green]PASS[/green]" if self.pass_ else "[red]FAIL[/red]"

    @property
    def delivery_str(self) -> str:
        if self.total == 0:
            return "—"
        pct = self.delivered * 100 // self.total
        color = "green" if pct >= 80 else "yellow" if pct >= 60 else "red"
        return f"[{color}]{self.delivered}/{self.total} ({pct}%)[/{color}]"

    @property
    def latency_str(self) -> str:
        if self.latency_ms < 0:
            return "—"
        color = "green" if self.latency_ms < 20 else \
                "yellow" if self.latency_ms < 100 else "red"
        return f"[{color}]{self.latency_ms} ms[/{color}]"


# ── Device stats snapshot ─────────────────────────────────────────────────

class DeviceStats:
    def __init__(self, role: str):
        self.role   = role
        self.rx     = 0
        self.tx     = 0
        self.errors = 0
        self.state  = "[dim]waiting[/dim]"
        self.node_id: Optional[int] = None


# ── Reporter ──────────────────────────────────────────────────────────────

EXPECTED_TESTS = [
    "connectivity",
    "single_hop",
    "multi_hop",
    "broadcast",
    "security",
    "stress",
    "rssi",
]


class Reporter:
    """
    Live terminal display.  Call start() before feeding events,
    stop() when done.  All public methods are thread-safe.
    """

    def __init__(self) -> None:
        self._lock    = threading.Lock()
        self._console = Console()
        self._live:   Optional[Live] = None

        self._results: Dict[str, TestResult] = {
            name: TestResult(name) for name in EXPECTED_TESTS
        }
        self._devices: Dict[str, DeviceStats] = {}
        self._start_time = time.monotonic()
        self._done = False

    # ── Lifecycle ─────────────────────────────────────────────────────────

    def start(self) -> None:
        self._start_time = time.monotonic()
        self._live = Live(
            self._render(),
            console=self._console,
            refresh_per_second=4,
            transient=False,
        )
        self._live.__enter__()

    def stop(self) -> None:
        if self._live:
            with self._lock:
                self._done = True
            self._live.update(self._render())
            self._live.__exit__(None, None, None)
        self._print_summary()

    # ── Event handlers ────────────────────────────────────────────────────

    def on_event(self, role: str, ev: dict) -> None:
        """Feed a parsed JSON event from a device."""
        etype = ev.get("event", "")
        data  = ev.get("data", {})

        with self._lock:
            dev = self._devices.setdefault(role, DeviceStats(role))

            if etype == "ready":
                dev.node_id = data.get("node_id")
                dev.state   = "[cyan]ready[/cyan]"

            elif etype == "joined":
                dev.state = "[cyan]joined[/cyan]"

            elif etype == "tx":
                dev.tx += 1

            elif etype == "rx":
                dev.rx += 1

            elif etype == "error":
                dev.errors += 1

            elif etype == "test_result":
                name = data.get("test", "")
                if name in self._results:
                    r = self._results[name]
                    r.pass_       = data.get("pass", False)
                    r.latency_ms  = data.get("latency_ms", -1)
                    r.delivered   = data.get("delivered", 0)
                    r.total       = data.get("total", 0)

            elif etype == "stats":
                dev.state = "[green]done[/green]"
                dev.tx    = data.get("tx", dev.tx)
                dev.rx    = data.get("rx", dev.rx)

        if self._live:
            self._live.update(self._render())

    # ── Rendering ─────────────────────────────────────────────────────────

    def _render(self) -> Panel:
        elapsed = time.monotonic() - self._start_time

        # Test results table
        t = Table(
            title="Test Results",
            box=box.ROUNDED,
            show_header=True,
            header_style="bold magenta",
            expand=True,
        )
        t.add_column("Test",       style="bold", min_width=16)
        t.add_column("Status",     justify="center", min_width=8)
        t.add_column("Delivery",   justify="right",  min_width=18)
        t.add_column("Latency",    justify="right",  min_width=10)

        with self._lock:
            results  = dict(self._results)
            devices  = dict(self._devices)

        for name in EXPECTED_TESTS:
            r = results[name]
            t.add_row(name, r.status_str, r.delivery_str, r.latency_str)

        # Device status table
        d = Table(
            title="Devices",
            box=box.ROUNDED,
            show_header=True,
            header_style="bold cyan",
            expand=True,
        )
        d.add_column("Role",    style="bold", min_width=12)
        d.add_column("Node",    justify="center", min_width=6)
        d.add_column("State",   justify="center", min_width=12)
        d.add_column("TX",      justify="right",  min_width=6)
        d.add_column("RX",      justify="right",  min_width=6)
        d.add_column("Errors",  justify="right",  min_width=7)

        for role in ("coordinator", "router", "end_node"):
            dev = devices.get(role)
            if dev:
                node = str(dev.node_id) if dev.node_id is not None else "?"
                d.add_row(role, node, dev.state,
                          str(dev.tx), str(dev.rx), str(dev.errors))
            else:
                d.add_row(role, "?", "[dim]not connected[/dim]",
                          "—", "—", "—")

        cols = Columns([t, d], equal=True, expand=True)

        title_color = "green" if self._done else "yellow"
        title = (f"[{title_color}]µMesh Hardware Integration Tests[/{title_color}]"
                 f"  [dim]elapsed: {elapsed:.1f}s[/dim]")
        return Panel(cols, title=title)

    # ── Summary ───────────────────────────────────────────────────────────

    def _print_summary(self) -> None:
        passed  = sum(1 for r in self._results.values() if r.pass_ is True)
        failed  = sum(1 for r in self._results.values() if r.pass_ is False)
        pending = sum(1 for r in self._results.values() if r.pass_ is None)
        total   = len(self._results)
        elapsed = time.monotonic() - self._start_time

        self._console.rule("[bold]Summary[/bold]")
        self._console.print(
            f"  Tests: {total}  "
            f"[green]Passed: {passed}[/green]  "
            f"[red]Failed: {failed}[/red]  "
            f"[dim]Pending: {pending}[/dim]  "
            f"  Elapsed: {elapsed:.1f}s"
        )
        if failed == 0 and pending == 0:
            self._console.print("[bold green]All tests passed.[/bold green]")
        elif failed > 0:
            self._console.print(
                f"[bold red]{failed} test(s) failed.[/bold red]"
            )
