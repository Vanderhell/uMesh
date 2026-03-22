#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/sec/keys.h"
#include "../src/sec/sec.h"

static int s_pass = 0;
static int s_fail = 0;

#define TEST_ASSERT(cond, name) \
    do { \
        if (cond) { \
            printf("  PASS: %s\n", name); \
            s_pass++; \
        } else { \
            printf("  FAIL: %s  (line %d)\n", name, __LINE__); \
            s_fail++; \
        } \
    } while (0)

/* NIST FIPS-197 test vector:
 * key    = 2b7e151628aed2a6abf7158809cf4f3c
 * plain  = 6bc1bee22e409f96e93d7e117393172a
 * cipher = 3ad77bb40d7a3660a89ecaf32466ef97
 */
static const uint8_t NIST_KEY[16] = {
    0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
    0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
};

static void test_keys_deterministic(void)
{
    uint8_t enc1[16], auth1[16];
    uint8_t enc2[16], auth2[16];

    keys_derive(NIST_KEY, 0x01, enc1, auth1);
    keys_derive(NIST_KEY, 0x01, enc2, auth2);

    TEST_ASSERT(memcmp(enc1,  enc2,  16) == 0, "keys: ENC_KEY deterministic");
    TEST_ASSERT(memcmp(auth1, auth2, 16) == 0, "keys: AUTH_KEY deterministic");
}

static void test_keys_different_net_id(void)
{
    uint8_t enc1[16], auth1[16];
    uint8_t enc2[16], auth2[16];

    keys_derive(NIST_KEY, 0x01, enc1, auth1);
    keys_derive(NIST_KEY, 0x02, enc2, auth2);

    TEST_ASSERT(memcmp(enc1,  enc2,  16) != 0, "keys: different net_id -> different ENC_KEY");
    TEST_ASSERT(memcmp(auth1, auth2, 16) != 0, "keys: different net_id -> different AUTH_KEY");
}

static void test_keys_enc_auth_differ(void)
{
    uint8_t enc[16], auth[16];
    keys_derive(NIST_KEY, 0x01, enc, auth);
    TEST_ASSERT(memcmp(enc, auth, 16) != 0, "keys: ENC_KEY != AUTH_KEY");
}

static void test_keys_not_master(void)
{
    uint8_t enc[16], auth[16];
    keys_derive(NIST_KEY, 0x01, enc, auth);
    TEST_ASSERT(memcmp(enc,  NIST_KEY, 16) != 0, "keys: ENC_KEY != MASTER_KEY");
    TEST_ASSERT(memcmp(auth, NIST_KEY, 16) != 0, "keys: AUTH_KEY != MASTER_KEY");
}

static void test_sec_encrypt_decrypt_roundtrip(void)
{
    umesh_frame_t frame;
    static const uint8_t payload[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    umesh_result_t r;

    memset(&frame, 0, sizeof(frame));
    frame.net_id      = 0x01;
    frame.dst         = 0x02;
    frame.src         = 0x03;
    frame.flags       = UMESH_FLAG_ENCRYPTED;
    frame.cmd         = UMESH_CMD_SENSOR_TEMP;
    frame.seq_num     = 0x0010;
    frame.hop_count   = 1;
    frame.payload_len = sizeof(payload);
    memcpy(frame.payload, payload, sizeof(payload));

    r = sec_init(NIST_KEY, 0x01, UMESH_SEC_FULL);
    TEST_ASSERT(r == UMESH_OK, "sec: init OK");

    r = sec_encrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_OK, "sec: encrypt OK");

    /* Payload should differ from original after encryption */
    TEST_ASSERT(memcmp(frame.payload, payload, sizeof(payload)) != 0,
                "sec: encrypted payload differs from plaintext");

    r = sec_decrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_OK, "sec: decrypt OK");

    /* Payload should be restored */
    TEST_ASSERT(memcmp(frame.payload, payload, sizeof(payload)) == 0,
                "sec: decrypted payload matches original");
}

static void test_sec_mic_tamper_detected(void)
{
    umesh_frame_t frame;
    static const uint8_t payload[] = { 0x11, 0x22, 0x33, 0x44 };
    umesh_result_t r;

    memset(&frame, 0, sizeof(frame));
    frame.net_id      = 0x01;
    frame.dst         = 0x02;
    frame.src         = 0x03;
    frame.flags       = UMESH_FLAG_ENCRYPTED;
    frame.cmd         = UMESH_CMD_PING;
    frame.seq_num     = 0x0011;
    frame.hop_count   = 1;
    frame.payload_len = sizeof(payload);
    memcpy(frame.payload, payload, sizeof(payload));

    r = sec_init(NIST_KEY, 0x01, UMESH_SEC_FULL);
    TEST_ASSERT(r == UMESH_OK, "mic-tamper: init OK");

    r = sec_encrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_OK, "mic-tamper: encrypt OK");

    /* Tamper with ciphertext (first byte of encrypted payload) */
    frame.payload[0] ^= 0xFF;

    r = sec_decrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_ERR_MIC_FAIL, "mic-tamper: tamper detected (MIC_FAIL)");
}

static void test_sec_replay_detected(void)
{
    umesh_frame_t frame;
    static const uint8_t payload[] = { 0xAA, 0xBB };
    umesh_result_t r;

    memset(&frame, 0, sizeof(frame));
    frame.net_id      = 0x01;
    frame.dst         = 0x02;
    frame.src         = 0x04;
    frame.flags       = UMESH_FLAG_ENCRYPTED;
    frame.cmd         = UMESH_CMD_PING;
    frame.seq_num     = 0x0020;
    frame.hop_count   = 1;
    frame.payload_len = sizeof(payload);
    memcpy(frame.payload, payload, sizeof(payload));

    r = sec_init(NIST_KEY, 0x01, UMESH_SEC_FULL);
    TEST_ASSERT(r == UMESH_OK, "replay: init OK");

    /* Encrypt and decrypt once (success) */
    r = sec_encrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_OK, "replay: first encrypt OK");
    r = sec_decrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_OK, "replay: first decrypt OK");

    /* Re-encrypt same frame (same seq_num) and try to replay */
    memcpy(frame.payload, payload, sizeof(payload));
    frame.payload_len = sizeof(payload);
    r = sec_encrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_OK, "replay: re-encrypt OK");
    r = sec_decrypt_frame(&frame);
    TEST_ASSERT(r == UMESH_ERR_REPLAY, "replay: duplicate seq_num rejected");
}

int main(void)
{
    printf("=== test_sec ===\n");
    test_keys_deterministic();
    test_keys_different_net_id();
    test_keys_enc_auth_differ();
    test_keys_not_master();
    test_sec_encrypt_decrypt_roundtrip();
    test_sec_mic_tamper_detected();
    test_sec_replay_detected();
    printf("Result: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
