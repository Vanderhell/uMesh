# Known Limitations

This document lists what the repository can and cannot claim from the current evidence.

## Verification categories

| Category | Status | Evidence |
| --- | --- | --- |
| Host verified | Verified | `VERIFICATION.md` and local POSIX build/test runs |
| CI verified | Verified by workflow configuration, not by every local run | `.github/workflows/ci.yml` |
| ESP32 compile verified | Compile/integration only | `port/esp32/` and `port/esp32/smoke/` |
| Hardware not verified | Not verified | No physical RF or device measurement evidence in this repository |

## What is intentionally not claimed

- Physical RF range
- Throughput or latency on real radios
- Current consumption or battery life on hardware
- Coexistence or regulatory compliance
- ESP32 flash-and-radio behavior unless a specific hardware report is recorded

## Versioning note

Package / release tags and wire protocol version are separate.

- Package / release tag: `v1.5.0`
- Wire protocol version: `UMESH_WIRE_VERSION`

Do not treat the tag as proof of protocol compatibility, and do not treat the wire version as a release tag.
