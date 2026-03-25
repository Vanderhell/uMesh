# Getting Started

This guide gets you from source checkout to a working 3-node mesh test.

## Step 1: Clone and build

```bash
git clone https://github.com/Vanderhell/uMesh
cd uMesh
git submodule update --init --recursive
cmake -S . -B build -DUMESH_PORT=posix
cmake --build build -j
```

## Step 2: Flash to ESP32

Use the auto-role firmware:

- `tests/hardware/firmware/auto_mesh_node/auto_mesh_node.ino`

Arduino IDE checklist per chip:

- Select the correct board (ESP32 / S2 / S3 / C3 / C6)
- Select correct USB/COM port
- Keep CPU/flash defaults unless your board vendor says otherwise
- Set `USB CDC On Boot` to `Enabled` (important for serial test tooling)

Flash one device as coordinator candidate, one as router candidate, one as end-node candidate.

## Step 3: Run test runner

```bash
pip install -r tests/hardware/runner/requirements.txt
python tests/hardware/runner/test_runner.py \
  -c COM_COORDINATOR -r COM_ROUTER -e COM_ENDNODE
```

Expected result:

- `7/7 PASS`
