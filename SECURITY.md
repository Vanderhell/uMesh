# Security (disclosure + limitations)

This project implements cryptographic mechanisms, but this repository does **not** include an independent security audit report. Treat the security properties described here as **implementation notes**, not as a guarantee of real-world resistance.

## Implemented (code-visible)

Implemented in `src/sec/`:
- AES-128 in CTR mode (payload encryption when `UMESH_SEC_FULL`)
- HMAC-SHA256, truncated to a 4-byte MIC (message integrity / authenticity when `UMESH_SEC_AUTH` or `UMESH_SEC_FULL`)
- Replay detection using a per-source sliding window (`UMESH_SEQ_WINDOW = 32`)

Keying model:
- Pre-shared `MASTER_KEY` (16 bytes) provided via `umesh_cfg_t.master_key`
- Two derived working keys (`ENC_KEY`, `AUTH_KEY`) derived from `MASTER_KEY` + `NET_ID` (see `src/sec/keys.c`)

## What this does NOT provide (not implemented)

- No key exchange / pairing protocol (no Diffie–Hellman, no certificate-based trust, no forward secrecy)
- No device identity attestation (a device with the shared key can impersonate others at the protocol level)
- No secure boot / firmware integrity (out of scope for this repository)
- No mitigation for RF jamming / deliberate interference (DoS at PHY)

## Important limitations (implementation-bound)

### Truncated MIC (4 bytes)

The MIC is 32 bits (`UMESH_MIC_SIZE = 4`). This reduces packet overhead but also reduces the brute-force security margin compared to a full-length HMAC.

This repository does not contain a threat-model document that justifies the truncation for any particular deployment. Treat this as a design trade-off.

### Nonce construction and uniqueness assumptions

CTR-mode confidentiality requires nonce/counter uniqueness under a given key.

In this implementation (`src/sec/sec.c`), the AES-CTR nonce includes:
- `SRC_NODE`, `NET_ID`, and `SEQ_NUM` (2 bytes)
- a 3-byte `SALT` derived from `ENC_KEY`
- a per-block counter

NOT VERIFIED: whether the nonce construction is sufficient for all usage patterns and all resets/power cycles on real devices.

### RX verification integration

Security verification and decryption must run on the receive path to be meaningful. This repository’s documentation must not claim “authenticated mesh” unless RX-side MIC verification + replay checks are actually enforced by the stack configuration being used.

See `VERIFICATION.md` for what was tested in this run.

## Reporting security issues

If you discover a vulnerability, open a GitHub issue or (preferably) provide a minimal reproduction and affected commit/versions.
