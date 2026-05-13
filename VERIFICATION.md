# Verification / Evidence (strict)

Date of this verification run: **2026-05-13**  
Host environment: **Windows**, CMake generator **Visual Studio 17 2022**, toolchain **MSVC** (see CMake output below).

This file records what was **actually executed** in this repository during this run, and what was **not executed**.

## Build evidence (executed)

Commands executed:

1. Configure (POSIX port + tests):
   - `cmake -S . -B build_audit -DUMESH_PORT=posix -DUMESH_BUILD_TESTS=ON`

Observed output (excerpt):
- `-- Building for: Visual Studio 17 2022`
- `-- uMesh: microcrypt found and enabled`
- `-- uMesh: POSIX port (testing)`
- `-- Build files have been written to: .../build_audit`

2. Build (Release):
   - `cmake --build build_audit --config Release --parallel`

Outcome:
- Build succeeded (MSBuild, Release artifacts produced in `build_audit/`).

### Reverification after repository changes (executed)

After documentation and code changes in this working tree, the following was re-run:
- `cmake --build build_audit --config Release --parallel`
- `ctest --test-dir build_audit -C Release --output-on-failure`

Outcome:
- Build succeeded.
- `ctest` again reported `100% tests passed, 0 tests failed out of 13`.

## Test evidence (executed)

Command executed:
- `ctest --test-dir build_audit -C Release --output-on-failure`

Observed result:
- `100% tests passed, 0 tests failed out of 13`

CTest listed tests (13):
- `test_crc`
- `test_ring`
- `test_frame`
- `test_sec`
- `test_posix_loopback`
- `test_mac`
- `test_routing`
- `test_net`
- `test_e2e`
- `test_election`
- `test_gradient`
- `test_power`
- `test_caps`

## CI evidence (configuration only)

Repository contains a GitHub Actions workflow:
- `.github/workflows/ci.yml`

This run did **not** execute GitHub Actions. CI pass/fail status on GitHub is **NOT VERIFIED** here.

## NOT RUN (not verified in this run)

The following were not executed or cannot be verified in this environment/run:
- ESP-IDF builds (ESP32/S2/S3/C3/C6) and any on-device flashing
- Radio-range / throughput / latency measurements
- Power/current measurements on hardware
- Hardware test runner in `tests/hardware/` (serial + physical devices required)
- Any security audit / cryptographic review beyond code inspection
