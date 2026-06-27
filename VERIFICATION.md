# Verification / Evidence (strict)

Date of this verification run: **2026-06-27**
Host environment: **Windows**, CMake generator **Visual Studio 17 2022**, toolchains **MSVC** and **ClangCL**.

This file records what was actually executed in this repository during this run, and what was not executed.

## Build evidence (executed)

Commands executed in this working tree:

1. `cmake -S . -B build-debug -DUMESH_PORT=posix -DUMESH_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug`
2. `cmake --build build-debug --config Debug --parallel`
3. `ctest --test-dir build-debug -C Debug --output-on-failure`
4. `cmake -S . -B build-release -DUMESH_PORT=posix -DUMESH_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release`
5. `cmake --build build-release --config Release --parallel`
6. `ctest --test-dir build-release -C Release --output-on-failure`
7. `cmake -S . -B build-security-off -DUMESH_PORT=posix -DUMESH_BUILD_TESTS=ON -DUMESH_USE_MICROCRYPT=OFF -DCMAKE_BUILD_TYPE=Debug`
8. `cmake --build build-security-off --config Debug --parallel`
9. `ctest --test-dir build-security-off -C Debug --output-on-failure`
10. `cmake -S . -B build-security-on -DUMESH_PORT=posix -DUMESH_BUILD_TESTS=ON -DUMESH_USE_MICROCRYPT=ON -DCMAKE_BUILD_TYPE=Debug`
11. `cmake --build build-security-on --config Debug --parallel`
12. `ctest --test-dir build-security-on -C Debug --output-on-failure`
13. `cmake -S . -B build-clang -DUMESH_PORT=posix -DUMESH_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -T ClangCL`
14. `cmake --build build-clang --config Debug --parallel`
15. `ctest --test-dir build-clang -C Debug --output-on-failure`

Additional builds executed and passed:
- `build-power-off`
- `build-lowmem`

Outcome:
- All listed POSIX builds succeeded.
- All listed `ctest` runs reported `100% tests passed`.

## Test evidence (executed)

Observed result for each listed test directory:
- `100% tests passed, 0 tests failed out of 15`

CTest listed tests (15):
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
- `test_corpus_smoke`
- `test_gradient`
- `test_power`
- `test_forwarding`
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
