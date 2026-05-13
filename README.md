# µMesh (uMesh)

Strict documentation policy: every important claim is either verified by repository contents and/or commands executed in this run, explicitly marked **NOT VERIFIED**, or removed.

## Status (this run)

**Verified by test execution (Windows / MSVC / CMake, POSIX port):**
- CMake configure/build succeeded for `-DUMESH_PORT=posix -DUMESH_BUILD_TESTS=ON`
- `ctest` ran **13 tests** and reported **100% pass (13/13)**  
  Evidence: `VERIFICATION.md`

**CI (configuration only):**
- GitHub Actions workflow exists for Ubuntu + Windows POSIX builds: `.github/workflows/ci.yml`  
  NOT VERIFIED: whether CI is currently passing on GitHub.

## What is implemented (code-visible)

µMesh is a C99 library that implements a small mesh-style protocol on top of a “raw 802.11 frame” PHY abstraction with separate modules for:
- PHY abstraction: `src/phy/` (+ ESP32 and POSIX ports in `port/`)
- MAC: `src/mac/` (send path includes CSMA/CA/backoff + ACK/retry logic)
- Network: `src/net/` (discovery + routing modes)
- Security: `src/sec/` (AES-128-CTR + HMAC-SHA256 truncated MIC + replay window)
- Optional power-management module: `src/power/` (compile-time gated)

## Verification and evidence

See:
- `VERIFICATION.md` (commands run, results, and what was NOT run)
- `docs/DIAGRAMS.md` (Mermaid diagrams derived from the code paths)
- `SECURITY.md` (threat model + limitations; no “strong security” claims without proof)

## Version consistency

**Implemented version in code:** `1.4.0` (`src/common/defs.h` defines `UMESH_VERSION_MAJOR/MINOR/PATCH`).  
NOT VERIFIED: Git tags / GitHub releases in this run (no network access).

## Getting started (API)

The public header is `include/umesh.h`. For configuration fields and defaults, see `docs/API_REFERENCE.md` (defaults are derived from code in `src/umesh.c` and constants in `src/common/defs.h`).

## What is NOT VERIFIED (intentionally not claimed here)

This repository (and this run) does not provide measured evidence for radio range, throughput, latency, or current consumption. Any such numbers must be backed by reproducible measurement artifacts (hardware, antenna, channel, TX power, environment) and linked from documentation.

## Documentation index

- `docs/API_REFERENCE.md`
- `docs/SECURITY_LAYER.md`
- `docs/PHYSICAL_LAYER.md`
- `docs/MAC_LAYER.md`
- `docs/NETWORK_LAYER.md`
- `docs/POWER_MANAGEMENT.md`
- `docs/KNOWN_ISSUES.md`

## License

MIT (see `LICENSE`).
