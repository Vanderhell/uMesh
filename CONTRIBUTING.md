# Contributing

This repository currently focuses on correctness and verifiable documentation.

## Before opening a PR

- Keep technical claims evidence-based. If you add performance/power/range claims, include reproducible measurement artifacts and link them.
- Separate host verified, CI verified, ESP32 compile verified, and hardware not verified in docs and review notes.
- Do not claim physical RF behavior, current consumption, throughput, latency, coexistence, or regulatory compliance without actual measurement evidence.
- Prefer small, reviewable changes (one logical change per PR).
- If you modify behavior, add or update a host-test under `tests/` when possible.
- If you only change documentation or community files, do not add code changes to the same PR unless explicitly requested.

## Pull request hygiene

- Fill in the PR template.
- If behavior changed, update the docs that describe the behavior.
- Do not add AI attribution or `Co-authored-by` trailers.

## Local build + tests (POSIX port)

The repository supports a host-testable POSIX port via CMake.

Example (Windows/MSVC):
- `cmake -S . -B build -DUMESH_PORT=posix -DUMESH_BUILD_TESTS=ON`
- `cmake --build build --config Release --parallel`
- `ctest --test-dir build -C Release --output-on-failure`

## Filing issues

Use the issue templates in `.github/ISSUE_TEMPLATE/` so reports include:

- commit SHA
- platform
- compiler/toolchain
- CMake options
- exact command
- expected behavior
- actual behavior
- logs
- host-simulated vs ESP32 compile vs real hardware
