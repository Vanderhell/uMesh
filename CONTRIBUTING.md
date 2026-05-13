# Contributing

This repository currently focuses on correctness and verifiable documentation.

## Before opening a PR

- Keep technical claims evidence-based. If you add performance/power/range claims, include reproducible measurement artifacts and link them.
- Prefer small, reviewable changes (one logical change per PR).
- If you modify behavior, add or update a host-test under `tests/` when possible.

## Local build + tests (POSIX port)

The repository supports a host-testable POSIX port via CMake.

Example (Windows/MSVC):
- `cmake -S . -B build -DUMESH_PORT=posix -DUMESH_BUILD_TESTS=ON`
- `cmake --build build --config Release --parallel`
- `ctest --test-dir build -C Release --output-on-failure`
