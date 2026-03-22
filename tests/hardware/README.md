# µMesh Hardware Integration Tests

End-to-end tests running on real hardware: one coordinator (ESP32-S3),
one router (ESP32-S3), and one end_node (ESP32-C3).

---

## Required hardware

| Device | Board | Role |
|---|---|---|
| 1× ESP32-S3 | any ESP32-S3 devkit | coordinator |
| 1× ESP32-S3 | any ESP32-S3 devkit | router |
| 1× ESP32-C3 | any ESP32-C3 devkit | end_node |

All three boards must be within WiFi range of each other (same room is fine).
Connect all three to your PC via USB before running the test runner.

---

## Topology

```
coordinator (0x01) <--WiFi--> router (0x02) <--WiFi--> end_node (0x03)
      |                                                       |
      +--- direct link (if in range) ------------------------+
```

The coordinator assigns NODE_IDs during the JOIN phase.

---

## Step 1 — Install µMesh as an Arduino library

Copy (or symlink) the µMesh repository into your Arduino libraries folder:

```
# Windows
xcopy /E /I "C:\Users\<you>\Desktop\µMesh" "%USERPROFILE%\Documents\Arduino\libraries\umesh"

# macOS / Linux
cp -r /path/to/umesh ~/Arduino/libraries/umesh
```

The Arduino IDE must see `umesh/include/umesh.h` and the source files
under `umesh/src/`.

---

## Step 2 — Install Arduino cores

In Arduino IDE (or arduino-cli):

```bash
# ESP32 core (covers both S3 and C3)
arduino-cli core install esp32:esp32
```

Minimum supported ESP-IDF version bundled with the core: **v5.1**.

---

## Step 3 — Flash the firmware

### Using Arduino IDE

1. Open `firmware/coordinator/coordinator.ino` — select board **ESP32S3 Dev Module**, flash.
2. Open `firmware/router/router.ino`           — select board **ESP32S3 Dev Module**, flash.
3. Open `firmware/end_node/end_node.ino`        — select board **ESP32C3 Dev Module**, flash.

### Using arduino-cli

```bash
# Coordinator (replace PORT with the actual port, e.g. COM3 or /dev/ttyUSB0)
arduino-cli compile --fqbn esp32:esp32:esp32s3 firmware/coordinator
arduino-cli upload  --fqbn esp32:esp32:esp32s3 --port PORT firmware/coordinator

# Router
arduino-cli compile --fqbn esp32:esp32:esp32s3 firmware/router
arduino-cli upload  --fqbn esp32:esp32:esp32s3 --port PORT firmware/router

# End node
arduino-cli compile --fqbn esp32:esp32:esp32c3 firmware/end_node
arduino-cli upload  --fqbn esp32:esp32:esp32c3 --port PORT firmware/end_node
```

---

## Step 4 — Install Python dependencies

```bash
cd runner
pip install -r requirements.txt
```

---

## Step 5 — Run the test suite

```bash
cd runner

# List available serial ports if you are unsure which port is which
python test_runner.py --list-ports

# Run tests (replace COMx / /dev/ttyUSBx with actual port names)
python test_runner.py \
    --coordinator COM3 \
    --router      COM4 \
    --end-node    COM5
```

The runner:
1. Opens all three serial ports
2. Displays a live table that updates as events arrive
3. Waits for the coordinator to emit the final `stats` event
4. Prints a summary and exits with code `0` (all pass) or `1` (any fail)

---

## JSON protocol

Each firmware device emits one JSON object per line at 115200 baud.

| Event | Description |
|---|---|
| `ready` | Device initialized, channel set |
| `joined` | A node joined the network (coordinator only) |
| `tx` | A packet was sent |
| `rx` | A packet was received |
| `test_result` | Result of one test step (coordinator only) |
| `stats` | Final MAC statistics — signals test completion |
| `error` | An error occurred (umesh_result_t code + string) |

### Examples

```json
{"event":"ready",       "data":{"role":"coordinator","node_id":1,"channel":6}}
{"event":"joined",      "data":{"node_id":2,"net_id":1}}
{"event":"tx",          "data":{"dst":2,"cmd":"0x01","size":0}}
{"event":"rx",          "data":{"src":2,"cmd":"0x02","rssi":-55}}
{"event":"test_result", "data":{"test":"single_hop","pass":true,"latency_ms":-1,"delivered":48,"total":50}}
{"event":"stats",       "data":{"tx":312,"rx":301,"ack":295,"retry":8,"drop":3}}
{"event":"error",       "data":{"code":1,"msg":"NO_ACK"}}
```

---

## Test descriptions

| Test | Pass condition |
|---|---|
| `connectivity` | Both nodes respond to PING (PONG received) |
| `single_hop` | ≥ 85 % of 50 ACK'd packets delivered to router |
| `multi_hop` | ≥ 75 % of 50 ACK'd packets delivered to end_node |
| `broadcast` | ≥ 9 of 10 broadcast PINGs sent without error |
| `security` | Encrypted + ACK'd packet reaches router |
| `stress` | ≥ 80 % of 200 rapid packets sent without error |
| `rssi` | Informational — always passes; reports RSSI per node |

---

## Troubleshooting

**Nodes do not join within 30 s**
- Confirm all boards are powered and within range.
- Confirm all boards use the same `MASTER_KEY` and `CHANNEL`.
- Check that the coordinator firmware is flashed first.

**`UMESH_ERR_HARDWARE` at startup**
- Check ESP-IDF version — minimum v5.1 required.
- Ensure the board has not been put into sleep or AP mode by other firmware.

**Python runner cannot open port**
- Check that no other program (Arduino Serial Monitor) has the port open.
- On Linux, you may need: `sudo usermod -aG dialout $USER` (then re-login).

**Test `stress` failing**
- Reduce `TX_POWER` if boards are very close (signal saturation).
- Increase `delay(5)` in the stress loop if the channel is congested.
