# µMesh — Security Layer

> AES-128 CTR · HMAC-SHA256 · Replay protection · Pre-shared key

---

## Overview

| Problem | Solution |
|---|---|
| **Confidentiality** | AES-128 CTR encryption |
| **Integrity** | HMAC-SHA256 MIC (4 bytes) |
| **Authenticity** | MIC covers both header and payload |
| **Replay protection** | Sliding window + SEQ_NUM |
| **Minimal overhead** | NONCE derived for free from SEQ_NUM |

---

## 1. Encryption — AES-128 CTR

```
keystream[i] = AES(ENC_KEY, NONCE || COUNTER[i])
encrypted_payload = plaintext XOR keystream
```

### NONCE — zero extra overhead

```
NONCE (16 bytes):
+--------+--------+---------+----------+-------------+
|SRC_NODE| NET_ID | SEQ_NUM |   SALT   |  COUNTER    |
|  1B    |  1B    |   2B    |    3B    |     9B      |
+--------+--------+---------+----------+-------------+

SALT = first 3 bytes of SHA-256(ENC_KEY)
     = constant derived from the key
```

All fields except SALT are already in the packet — **zero extra overhead.**

### SEQ_NUM overflow protection

```c
/* On SEQ_NUM overflow -> regenerate SALT */
if (++s_seq_num > 0x0FFF) {
    s_seq_num = 0;
    sec_regenerate_salt();
}
```

---

## 2. Integrity — HMAC-MIC

```
MIC = first 4 bytes of HMAC-SHA256(
    AUTH_KEY,
    NET_ID || DST || SRC || SEQ_NUM || encrypted_payload
)
```

MIC covers the header too — an attacker cannot change the address or SEQ_NUM.

**Why 4 bytes:**
```
32 bytes (full HMAC) = excessive overhead
4 bytes = 2^32 combinations
        + SEQ_NUM replay protection
        = practically unbreakable for this use case
```

**Operation order (Encrypt-then-MAC):**
```
1. Encrypt payload (AES-CTR)
2. Compute MIC from (header + encrypted payload)
3. Append MIC after payload
```

---

## 3. Keys — derivation from MASTER_KEY

```c
/* One MASTER_KEY -> two working keys */
ENC_KEY  = AES(MASTER_KEY, 0x01 || NET_ID || padding)
AUTH_KEY = AES(MASTER_KEY, 0x02 || NET_ID || padding)
```

Configuration — only one key needs to be shared:

```c
umesh_cfg_t cfg = {
    .master_key = {
        0x2B, 0x7E, 0x15, 0x16,
        0x28, 0xAE, 0xD2, 0xA6,
        0xAB, 0xF7, 0x15, 0x88,
        0x09, 0xCF, 0x4F, 0x3C
    }
};
```

---

## 4. Replay protection — Sliding Window

```
last_seq = 100, window = 32 bits

SEQ 101 -> new   OK
SEQ  99 -> within window, not yet seen   OK
SEQ  68 -> too old   X
SEQ 100 -> duplicate   X
```

```c
#define UMESH_SEQ_WINDOW   32

typedef struct {
    uint8_t  src_node;
    uint16_t last_seq;
    uint32_t window_bitmap;
} umesh_replay_state_t;
```

---

## 5. Security levels

```c
typedef enum {
    UMESH_SEC_NONE = 0x00,  /* testing / debug */
    UMESH_SEC_AUTH = 0x01,  /* MIC only */
    UMESH_SEC_FULL = 0x02,  /* AES-CTR + HMAC-MIC */
} umesh_security_t;
```

---

## 6. What is encrypted

```
[802.11 HEADER]  ->  no (hardware routing)
[UMESH MAC HDR]  ->  no (software routing)
[UMESH NET HDR]  ->  no (software routing)
[OPCODE       ]  ->  YES ENCRYPTED
[PAYLOAD      ]  ->  YES ENCRYPTED
[MIC          ]  ->  HMAC of (header + encrypted data)
[CRC16        ]  ->  no (PHY/MAC)
```

---

## 7. Overhead

```
AES-128 CTR:   0 bytes (NONCE is free)
MIC:           4 bytes
-----------------------
Total:         4 bytes / packet
```

---

## 8. Implementation — microcrypt

```c
/* sec/sec.h -- internal */

umesh_result_t sec_init(const uint8_t *master_key,
                        uint8_t net_id,
                        umesh_security_t level);

umesh_result_t sec_encrypt_frame(umesh_frame_t *frame);
/* 1. AES-CTR encryption
 * 2. MIC computation and append */

umesh_result_t sec_decrypt_frame(umesh_frame_t *frame);
/* 1. Verify MIC  -> UMESH_ERR_MIC_FAIL
 * 2. Verify replay -> UMESH_ERR_REPLAY
 * 3. AES-CTR decryption */

void sec_derive_keys(const uint8_t *master_key,
                     uint8_t net_id,
                     uint8_t *enc_key,
                     uint8_t *auth_key);
```

---

## 9. Constant-time MIC verification

```c
/* Protection against timing attacks */
bool mic_verify_ct(const uint8_t *a,
                   const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++)
        diff |= a[i] ^ b[i];
    return diff == 0;
}
```

---

## 10. Security analysis

| Attack | Protection |
|---|---|
| Passive eavesdropping | AES-128 CTR |
| Packet modification | HMAC-MIC |
| Packet forgery | HMAC-MIC + shared key |
| Replay attack | SEQ_NUM sliding window |
| Timing attack | Constant-time compare |

### Known limitations

| Attack | Note |
|---|---|
| DoS jamming | Physical layer — not solvable in software |
| Traffic analysis | Attacker can see that communication is occurring |
| Key compromise | Pre-shared key — DH exchange planned for future |

---

## 11. Constants

```c
#define UMESH_KEY_SIZE      16
#define UMESH_MIC_SIZE       4
#define UMESH_NONCE_SIZE    16
#define UMESH_SEQ_WINDOW    32
```
