EXEC ONLY.

Work in Vanderhell/uMesh.

Continue only after implementation and verification gates are green.
Do not tag. Do not create GitHub release. Do not push release tags.
No AI attribution.

Goal:
update docs/version/release metadata to match only verified behavior.

Inspect:
- git status --short
- git tag --list --sort=version:refname
- git show-ref --tags
- git describe --tags --always --dirty
- grep -R "1\.4\.0\|2\.0\.0\|UMESH_VERSION\|project(.*umesh\|TWT\|power loss\|multi-hop\|nonce\|CCA\|13/13" -n . --exclude-dir=.git

Required docs:
- README.md
- SECURITY.md
- VERIFICATION.md
- API reference
- wire protocol docs
- MAC docs
- routing docs
- discovery/election docs
- power docs
- known issues
- examples
- Arduino metadata
- migration notes from 1.x

Documentation rules:
1. Separate implemented/test-confirmed, compile-confirmed, host-simulated, hardware-confirmed, not verified, and known limitations.
2. Remove false or unverified claims:
   - nonce wrap fixed by deterministic salt recalculation
   - full multi-hop without forwarding tests
   - software CCA when only RX callback state is checked
   - actual light sleep when only counters are updated
   - TWT capability placeholder
   - old 13/13 host tests proving mesh readiness
3. VERIFICATION.md must contain only commands/results executed in this task.
4. Hardware RF/range/current/real multi-device behavior remains NOT VERIFIED unless physically tested.
5. Keep the next project release target at v1.5.0 and do not create it until mandatory gates pass.
6. Do not create a tag.
7. Do not mark production-ready without physical multi-device ESP32 evidence.
8. Preserve MIT license.
9. Do not add marketing/future-roadmap text.

Run:
- cmake -S . -B build-doc-check -DUMESH_PORT=posix -DUMESH_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
- cmake --build build-doc-check --parallel
- ctest --test-dir build-doc-check --output-on-failure
- git diff --check
- git status --short

Commit only docs/version metadata:
release: set next umesh release target to v1.5.0
