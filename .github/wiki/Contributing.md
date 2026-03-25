# Contributing

Thanks for helping improve µMesh.

## Ways to contribute

- Report bugs with logs and reproducible steps
- Propose features with clear use case
- Improve docs, examples, and tests
- Submit fixes for portability, reliability, or performance

## Development workflow

1. Fork and create a feature branch.
2. Keep changes focused and small.
3. Add or update tests when behavior changes.
4. Run local build/tests before opening PR.
5. Open a Pull Request with clear summary and rationale.

## Local build and test

```bash
git submodule update --init --recursive
cmake -S . -B build -DUMESH_PORT=posix -DUMESH_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Coding expectations

- Keep C99 style consistent with existing code
- Avoid breaking public API without discussion
- Prefer explicit error handling and clear return codes
- Document non-obvious behavior in `docs/`

## Pull request checklist

- [ ] Compiles on your target configuration
- [ ] Tests pass or failures are explained
- [ ] Docs updated (README/docs/wiki) if needed
- [ ] Backward compatibility impact described
