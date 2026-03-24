"""
device.py — Thread-safe serial device abstraction.

Each Device opens a serial port, reads JSON lines in a background thread,
and dispatches events to registered callbacks.
"""

import json
import threading
import time
from typing import Callable, Dict, List, Optional


class Device:
    """
    Reads JSON events from a serial port in a background thread.

    Each line on the port must be a complete JSON object, e.g.:
        {"event":"rx","data":{"src":2,"cmd":"0x01","rssi":-55}}

    Usage:
        dev = Device("coordinator", "/dev/ttyUSB0", baud=115200)
        dev.on("test_result", lambda ev: ...)
        dev.start()
        ...
        dev.stop()
    """

    def __init__(self, role: str, port: str, baud: int = 115200):
        self.role  = role
        self.port  = port
        self.baud  = baud

        self._serial   = None
        self._thread   = None
        self._running  = False
        self._lock     = threading.Lock()

        # event_type -> list of callbacks
        self._handlers: Dict[str, List[Callable]] = {}

        # All events received, in order (thread-safe via lock)
        self._events: List[dict] = []

    # ── Registration ──────────────────────────────────────────────────────

    def on(self, event: str, callback: Callable) -> None:
        """Register a callback for a specific event type."""
        with self._lock:
            self._handlers.setdefault(event, []).append(callback)

    def on_any(self, callback: Callable) -> None:
        """Register a callback that fires for every event."""
        self.on("*", callback)

    # ── Lifecycle ─────────────────────────────────────────────────────────

    def start(self) -> None:
        import serial  # imported here so import errors are clear
        self._serial  = serial.Serial(
            self.port,
            self.baud,
            timeout=1,
            write_timeout=1,
        )
        self._running = True
        self._thread  = threading.Thread(
            target=self._read_loop,
            name=f"device-{self.role}",
            daemon=True,
        )
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=3)
        if self._serial and self._serial.is_open:
            self._serial.close()

    def is_alive(self) -> bool:
        return self._thread is not None and self._thread.is_alive()

    def clear_events(self) -> None:
        """Drop all buffered events collected so far."""
        with self._lock:
            self._events.clear()

    def send_command(self, command: str) -> None:
        """
        Send an ASCII command line to the device serial console.
        Appends newline when missing.
        """
        line = command if command.endswith("\n") else f"{command}\n"
        payload = line.encode("ascii", errors="ignore")
        with self._lock:
            if not self._serial or not self._serial.is_open:
                raise RuntimeError(f"{self.role} serial port is not open")
            self._serial.write(payload)
            self._serial.flush()

    # ── Event access ──────────────────────────────────────────────────────

    def events(self) -> List[dict]:
        """Return a snapshot of all received events."""
        with self._lock:
            return list(self._events)

    def events_of(self, event_type: str) -> List[dict]:
        """Return all events of a specific type."""
        return [e for e in self.events() if e.get("event") == event_type]

    def wait_for(self, event_type: str,
                 timeout: float = 30.0) -> Optional[dict]:
        """
        Block until an event of the given type arrives, then return it.
        Returns None on timeout.
        """
        deadline = time.monotonic() + timeout
        seen = 0
        while time.monotonic() < deadline:
            evs = self.events_of(event_type)
            if len(evs) > seen:
                return evs[-1]
            time.sleep(0.05)
        return None

    # ── Internal ──────────────────────────────────────────────────────────

    def _read_loop(self) -> None:
        while self._running:
            try:
                raw = self._serial.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                self._dispatch(line)
            except Exception:
                # Serial errors (device unplugged, etc.) — stop gracefully
                self._running = False
                break

    def _dispatch(self, line: str) -> None:
        try:
            ev = json.loads(line)
        except json.JSONDecodeError:
            # Non-JSON output from firmware (e.g. boot messages) — ignore
            return

        if "event" not in ev:
            return

        with self._lock:
            self._events.append(ev)
            handlers = (
                list(self._handlers.get(ev["event"], []))
                + list(self._handlers.get("*", []))
            )

        for cb in handlers:
            try:
                cb(ev)
            except Exception:
                pass
